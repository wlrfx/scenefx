#include <assert.h>
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <scenefx/types/wlr_scene.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/gles2.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>

#include "render/tracy.h"
#include "scenefx/render/fx_renderer/fx_effect_framebuffers.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"
#include "scenefx/render/pass.h"
#include "scenefx/types/fx/blur_data.h"
#include "scenefx/types/fx/clipped_region.h"
#include "scenefx/types/linked_node.h"
#include "types/fx/clipped_region.h"
#include "types/wlr_output.h"
#include "types/wlr_scene.h"
#include "util/array.h"
#include "util/env.h"
#include "util/time.h"
#include "wlr/util/box.h"

#include <wlr/config.h>

#if WLR_HAS_XWAYLAND
#include <wlr/xwayland/xwayland.h>
#endif

#define DMABUF_FEEDBACK_DEBOUNCE_FRAMES  30
#define HIGHLIGHT_DAMAGE_FADEOUT_TIME   250

struct wlr_scene_tree *wlr_scene_tree_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_TREE);
	struct wlr_scene_tree *tree = wl_container_of(node, tree, node);
	return tree;
}

struct wlr_scene_rect *wlr_scene_rect_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_RECT);
	struct wlr_scene_rect *rect = wl_container_of(node, rect, node);
	return rect;
}

struct wlr_scene_optimized_blur *wlr_scene_optimized_blur_from_node(
		struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR);
	struct wlr_scene_optimized_blur *blur_node =
		wl_container_of(node, blur_node, node);
	return blur_node;
}

struct wlr_scene_buffer *wlr_scene_buffer_from_node(
		struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_BUFFER);
	struct wlr_scene_buffer *buffer = wl_container_of(node, buffer, node);
	return buffer;
}

struct wlr_scene_shadow *wlr_scene_shadow_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_SHADOW);
	struct wlr_scene_shadow *shadow = wl_container_of(node, shadow, node);
	return shadow;
}

struct wlr_scene_drop_shadow *wlr_scene_drop_shadow_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_DROP_SHADOW);
	struct wlr_scene_drop_shadow *drop_shadow = wl_container_of(node, drop_shadow, node);
	return drop_shadow;
}

struct wlr_scene_blur *wlr_scene_blur_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_BLUR);
	struct wlr_scene_blur *blur = wl_container_of(node, blur, node);
	return blur;
}

struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node) {
	struct wlr_scene_tree *tree;
	if (node->type == WLR_SCENE_NODE_TREE) {
		tree = wlr_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree->node.parent != NULL) {
		tree = tree->node.parent;
	}
	struct wlr_scene *scene = wl_container_of(tree, scene, tree);
	return scene;
}

static void scene_node_init(struct wlr_scene_node *node,
		enum wlr_scene_node_type type, struct wlr_scene_tree *parent) {
	*node = (struct wlr_scene_node){
		.type = type,
		.parent = parent,
		.enabled = true,
	};

	wl_list_init(&node->link);

	wl_signal_init(&node->events.destroy);
	pixman_region32_init(&node->visible);

	if (parent != NULL) {
		wl_list_insert(parent->children.prev, &node->link);
	}

	wlr_addon_set_init(&node->addons);
}

struct highlight_region {
	pixman_region32_t region;
	struct timespec when;
	struct wl_list link;
};

static void scene_buffer_set_buffer(struct wlr_scene_buffer *scene_buffer,
	struct wlr_buffer *buffer);
static void scene_buffer_set_texture(struct wlr_scene_buffer *scene_buffer,
	struct wlr_texture *texture);

void wlr_scene_node_destroy(struct wlr_scene_node *node) {
	if (node == NULL) {
		return;
	}

	// We want to call the destroy listeners before we do anything else
	// in case the destroy signal would like to remove children before they
	// are recursively destroyed.
	wl_signal_emit_mutable(&node->events.destroy, NULL);
	wlr_addon_set_finish(&node->addons);

	wlr_scene_node_set_enabled(node, false);

	struct wlr_scene *scene = scene_node_get_root(node);
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		uint64_t active = scene_buffer->active_outputs;
		if (active) {
			struct wlr_scene_output *scene_output;
			wl_list_for_each(scene_output, &scene->outputs, link) {
				if (active & (1ull << scene_output->index)) {
					wl_signal_emit_mutable(&scene_buffer->events.output_leave,
						scene_output);
				}
			}
		}

		scene_buffer_set_buffer(scene_buffer, NULL);
		scene_buffer_set_texture(scene_buffer, NULL);
		pixman_region32_fini(&scene_buffer->opaque_region);
		wlr_drm_syncobj_timeline_unref(scene_buffer->wait_timeline);
		linked_node_destroy(&scene_buffer->drop_shadow);
		linked_node_destroy(&scene_buffer->blur);

		assert(wl_list_empty(&scene_buffer->events.output_leave.listener_list));
		assert(wl_list_empty(&scene_buffer->events.output_enter.listener_list));
		assert(wl_list_empty(&scene_buffer->events.outputs_update.listener_list));
		assert(wl_list_empty(&scene_buffer->events.output_sample.listener_list));
		assert(wl_list_empty(&scene_buffer->events.frame_done.listener_list));
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);

		if (scene_tree == &scene->tree) {
			assert(!node->parent);
			struct wlr_scene_output *scene_output, *scene_output_tmp;
			wl_list_for_each_safe(scene_output, scene_output_tmp, &scene->outputs, link) {
				wlr_scene_output_destroy(scene_output);
			}

			wl_list_remove(&scene->linux_dmabuf_v1_destroy.link);
			wl_list_remove(&scene->gamma_control_manager_v1_destroy.link);
			wl_list_remove(&scene->gamma_control_manager_v1_set_gamma.link);
		} else {
			assert(node->parent);
		}

		struct wlr_scene_node *child, *child_tmp;
		wl_list_for_each_safe(child, child_tmp,
				&scene_tree->children, link) {
			wlr_scene_node_destroy(child);
		}
	} else if (node->type == WLR_SCENE_NODE_DROP_SHADOW) {
		// Remove the drop-shadows referenced buffers link to this node
		struct wlr_scene_drop_shadow *scene_shadow = wlr_scene_drop_shadow_from_node(node);
		linked_node_destroy(&scene_shadow->buffer_source);
	} else if (node->type == WLR_SCENE_NODE_BLUR) {
		struct wlr_scene_blur *blur = wlr_scene_blur_from_node(node);
		linked_node_destroy(&blur->transparency_mask_source);
	}

	assert(wl_list_empty(&node->events.destroy.listener_list));

	wl_list_remove(&node->link);
	pixman_region32_fini(&node->visible);
	free(node);
}

static void scene_tree_init(struct wlr_scene_tree *tree,
		struct wlr_scene_tree *parent) {
	*tree = (struct wlr_scene_tree){0};
	scene_node_init(&tree->node, WLR_SCENE_NODE_TREE, parent);
	wl_list_init(&tree->children);
}

struct wlr_scene *wlr_scene_create(void) {
	struct wlr_scene *scene = calloc(1, sizeof(*scene));
	if (scene == NULL) {
		return NULL;
	}

	scene_tree_init(&scene->tree, NULL);

	wl_list_init(&scene->outputs);
	wl_list_init(&scene->linux_dmabuf_v1_destroy.link);
	wl_list_init(&scene->gamma_control_manager_v1_destroy.link);
	wl_list_init(&scene->gamma_control_manager_v1_set_gamma.link);

	const char *debug_damage_options[] = {
		"none",
		"rerender",
		"highlight",
		NULL
	};

	scene->debug_damage_option = env_parse_switch("WLR_SCENE_DEBUG_DAMAGE", debug_damage_options);
	scene->direct_scanout = !env_parse_bool("WLR_SCENE_DISABLE_DIRECT_SCANOUT");
	scene->calculate_visibility = !env_parse_bool("WLR_SCENE_DISABLE_VISIBILITY");
	scene->highlight_transparent_region = env_parse_bool("WLR_SCENE_HIGHLIGHT_TRANSPARENT_REGION");

	scene->blur_data = blur_data_get_default();

	return scene;
}

struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *parent) {
	assert(parent);

	struct wlr_scene_tree *tree = calloc(1, sizeof(*tree));
	if (tree == NULL) {
		return NULL;
	}

	scene_tree_init(tree, parent);
	return tree;
}

static void scene_node_get_size(struct wlr_scene_node *node, int *lx, int *ly);

typedef bool (*scene_node_box_iterator_func_t)(struct wlr_scene_node *node,
	int sx, int sy, void *data);

static bool _scene_nodes_in_box(struct wlr_scene_node *node, struct wlr_box *box,
		scene_node_box_iterator_func_t iterator, void *user_data, int lx, int ly) {
	if (!node->enabled) {
		return false;
	}

	switch (node->type) {
	case WLR_SCENE_NODE_TREE:;
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each_reverse(child, &scene_tree->children, link) {
			if (_scene_nodes_in_box(child, box, iterator, user_data, lx + child->x, ly + child->y)) {
				return true;
			}
		}
		break;
	case WLR_SCENE_NODE_OPTIMIZED_BLUR:;
	case WLR_SCENE_NODE_RECT:
	case WLR_SCENE_NODE_SHADOW:
	case WLR_SCENE_NODE_DROP_SHADOW:
	case WLR_SCENE_NODE_BUFFER:;
	case WLR_SCENE_NODE_BLUR:;
		struct wlr_box node_box = { .x = lx, .y = ly };
		scene_node_get_size(node, &node_box.width, &node_box.height);

		if (wlr_box_intersection(&node_box, &node_box, box) &&
				iterator(node, lx, ly, user_data)) {
			return true;
		}
		break;
	}

	return false;
}

static bool scene_nodes_in_box(struct wlr_scene_node *node, struct wlr_box *box,
		scene_node_box_iterator_func_t iterator, void *user_data) {
	int x, y;
	wlr_scene_node_coords(node, &x, &y);

	return _scene_nodes_in_box(node, box, iterator, user_data, x, y);
}

static pixman_region32_t create_corner_location_region(struct fx_corner_radii corners, int x, int y, int width, int height) {
	pixman_region32_t corner_region;
	pixman_region32_init(&corner_region);
	if (corners.top_left) {
		pixman_region32_union_rect(&corner_region, &corner_region, x, y, corners.top_left, corners.top_left);
	}

	if (corners.top_right) {
		pixman_region32_union_rect(&corner_region, &corner_region, x + (width - corners.top_right), y,
			corners.top_right, corners.top_right);
	}

	if (corners.bottom_left) {
		pixman_region32_union_rect(&corner_region, &corner_region, x, y + (height - corners.bottom_left),
			corners.bottom_left, corners.bottom_left);
	}

	if (corners.bottom_right) {
		pixman_region32_union_rect(&corner_region, &corner_region,
			x + (width - corners.bottom_right), y + (height - corners.bottom_right),
			corners.bottom_right, corners.bottom_right);
	}

	return corner_region;
}

static void scene_node_opaque_region(struct wlr_scene_node *node, int x, int y,
		pixman_region32_t *opaque) {
	int width, height;
	scene_node_get_size(node, &width, &height);

	if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);
		if (scene_rect->color[3] != 1) {
			return;
		}

		pixman_region32_fini(opaque);
		pixman_region32_init_rect(opaque, x, y, width, height);

		// subtract corners from opaque region
		if (!fx_corner_radii_is_empty(&scene_rect->corners)) {
			pixman_region32_t corners = create_corner_location_region(scene_rect->corners, x, y, width, height);
			pixman_region32_subtract(opaque, opaque, &corners);
			pixman_region32_fini(&corners);
		}

		// subtract clipped area from opaque region
		if (!wlr_box_empty(&scene_rect->clipped_region.area)) {
			struct wlr_box *clipped = &scene_rect->clipped_region.area;
			pixman_region32_t clipped_region;
			pixman_region32_init_rect(&clipped_region, clipped->x + x, clipped->y + y,
					clipped->width, clipped->height);
			pixman_region32_subtract(opaque, opaque, &clipped_region);
			pixman_region32_fini(&clipped_region);
		}
		return;
	} else if (node->type == WLR_SCENE_NODE_SHADOW) {
		// TODO: test & handle case of blur sigma = 0 and color[3] = 1?
		return;
	} else if (node->type == WLR_SCENE_NODE_DROP_SHADOW) {
		// TODO: test & handle case of blur sigma = 0 and color[3] = 1?
		return;
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		if (!scene_buffer->buffer) {
			return;
		}

		if (scene_buffer->opacity != 1) {
			return;
		}

		if (!scene_buffer->buffer_is_opaque) {
			pixman_region32_copy(opaque, &scene_buffer->opaque_region);
			pixman_region32_intersect_rect(opaque, opaque, 0, 0, width, height);
			pixman_region32_translate(opaque, x, y);
		} else {
			pixman_region32_fini(opaque);
			pixman_region32_init_rect(opaque, x, y, width, height);
		}

		// subtract the corners from the opaque region
		if (!fx_corner_radii_is_empty(&scene_buffer->corners)) {
			pixman_region32_t corners = create_corner_location_region(scene_buffer->corners, x, y, width, height);
			pixman_region32_subtract(opaque, opaque, &corners);
			pixman_region32_fini(&corners);
		}

		return;
	} else if (node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR || node->type == WLR_SCENE_NODE_BLUR) {
		// Always transparent
		return;
	}

	unreachable();
}

struct scene_update_data {
	pixman_region32_t *visible;
	pixman_region32_t *update_region;
	struct wlr_box update_box;
	struct wl_list *outputs;
	bool calculate_visibility;

#if WLR_HAS_XWAYLAND
	struct wlr_xwayland_surface *restack_above;
#endif

	bool optimized_blur_dirty;
	struct blur_data *blur_data;
};

static uint32_t region_area(pixman_region32_t *region) {
	uint32_t area = 0;

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(region, &nrects);
	for (int i = 0; i < nrects; ++i) {
		area += (rects[i].x2 - rects[i].x1) * (rects[i].y2 - rects[i].y1);
	}

	return area;
}

static void scale_region(pixman_region32_t *region, float scale, bool round_up) {
	wlr_region_scale(region, region, scale);

	if (round_up && floor(scale) != scale) {
		wlr_region_expand(region, region, 1);
	}
}

struct render_data {
	enum wl_output_transform transform;
	float scale;
	struct wlr_box logical;
	int trans_width, trans_height;

	struct wlr_scene_output *output;

	struct fx_gles_render_pass *render_pass;
	pixman_region32_t damage;

	bool has_blur;
};

static void logical_to_buffer_coords(pixman_region32_t *region, const struct render_data *data,
		bool round_up) {
	enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
	scale_region(region, data->scale, round_up);
	wlr_region_transform(region, region, transform, data->trans_width, data->trans_height);
}

static void output_to_buffer_coords(pixman_region32_t *damage, struct wlr_output *output) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	wlr_region_transform(damage, damage,
		wlr_output_transform_invert(output->transform), width, height);
}

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

static void transform_output_box(struct wlr_box *box, const struct render_data *data) {
	enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
	scale_box(box, data->scale);
	wlr_box_transform(box, box, transform, data->trans_width, data->trans_height);
}

static void scene_output_damage(struct wlr_scene_output *scene_output,
		const pixman_region32_t *damage) {
	struct wlr_output *output = scene_output->output;

	pixman_region32_t clipped;
	pixman_region32_init(&clipped);
	pixman_region32_intersect_rect(&clipped, damage, 0, 0, output->width, output->height);

	if (!pixman_region32_empty(&clipped)) {
		wlr_output_schedule_frame(scene_output->output);
		wlr_damage_ring_add(&scene_output->damage_ring, &clipped);

		pixman_region32_union(&scene_output->pending_commit_damage,
			&scene_output->pending_commit_damage, &clipped);
	}

	pixman_region32_fini(&clipped);
}

static void scene_output_damage_whole(struct wlr_scene_output *scene_output) {
	struct wlr_output *output = scene_output->output;

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, output->width, output->height);
	scene_output_damage(scene_output, &damage);
	pixman_region32_fini(&damage);
}

static void scene_damage_outputs(struct wlr_scene *scene, pixman_region32_t *damage) {
	if (pixman_region32_empty(damage)) {
		return;
	}

	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		pixman_region32_t output_damage;
		pixman_region32_init(&output_damage);
		pixman_region32_copy(&output_damage, damage);
		pixman_region32_translate(&output_damage,
			-scene_output->x, -scene_output->y);
		scale_region(&output_damage, scene_output->output->scale, true);
		output_to_buffer_coords(&output_damage, scene_output->output);
		scene_output_damage(scene_output, &output_damage);
		pixman_region32_fini(&output_damage);
	}
}

static void update_node_update_outputs(struct wlr_scene_node *node,
		struct wl_list *outputs, struct wlr_scene_output *ignore,
		struct wlr_scene_output *force) {
	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return;
	}

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

	uint32_t largest_overlap = 0;
	struct wlr_scene_output *old_primary_output = scene_buffer->primary_output;
	scene_buffer->primary_output = NULL;

	size_t count = 0;
	uint64_t active_outputs = 0;

	// let's update the outputs in two steps:
	//  - the primary outputs
	//  - the enter/leave signals
	// This ensures that the enter/leave signals can rely on the primary output
	// to have a reasonable value. Otherwise, they may get a value that's in
	// the middle of a calculation.
	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, outputs, link) {
		if (scene_output == ignore) {
			continue;
		}

		if (!scene_output->output->enabled) {
			continue;
		}

		struct wlr_box output_box = {
			.x = scene_output->x,
			.y = scene_output->y,
		};
		wlr_output_effective_resolution(scene_output->output,
			&output_box.width, &output_box.height);

		pixman_region32_t intersection;
		pixman_region32_init(&intersection);
		pixman_region32_intersect_rect(&intersection, &node->visible,
			output_box.x, output_box.y, output_box.width, output_box.height);

		if (!pixman_region32_empty(&intersection)) {
			uint32_t overlap = region_area(&intersection);
			if (overlap >= largest_overlap) {
				largest_overlap = overlap;
				scene_buffer->primary_output = scene_output;
			}

			active_outputs |= 1ull << scene_output->index;
			count++;
		}

		pixman_region32_fini(&intersection);
	}

	if (old_primary_output != scene_buffer->primary_output) {
		scene_buffer->prev_feedback_options =
			(struct wlr_linux_dmabuf_feedback_v1_init_options){0};
	}

	uint64_t old_active = scene_buffer->active_outputs;
	scene_buffer->active_outputs = active_outputs;

	wl_list_for_each(scene_output, outputs, link) {
		uint64_t mask = 1ull << scene_output->index;
		bool intersects = active_outputs & mask;
		bool intersects_before = old_active & mask;

		if (intersects && !intersects_before) {
			wl_signal_emit_mutable(&scene_buffer->events.output_enter, scene_output);
		} else if (!intersects && intersects_before) {
			wl_signal_emit_mutable(&scene_buffer->events.output_leave, scene_output);
		}
	}

	// if there are active outputs on this node, we should always have a primary
	// output
	assert(!scene_buffer->active_outputs || scene_buffer->primary_output);

	// Skip output update event if nothing was updated
	if (old_active == active_outputs &&
			(!force || ((1ull << force->index) & ~active_outputs)) &&
			old_primary_output == scene_buffer->primary_output) {
		return;
	}

	struct wlr_scene_output *outputs_array[64];
	struct wlr_scene_outputs_update_event event = {
		.active = outputs_array,
		.size = count,
	};

	size_t i = 0;
	wl_list_for_each(scene_output, outputs, link) {
		if (~active_outputs & (1ull << scene_output->index)) {
			continue;
		}

		assert(i < count);
		outputs_array[i++] = scene_output;
	}

	wl_signal_emit_mutable(&scene_buffer->events.outputs_update, &event);
}

#if WLR_HAS_XWAYLAND
static struct wlr_xwayland_surface *scene_node_try_get_managed_xwayland_surface(
		struct wlr_scene_node *node) {
	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	struct wlr_scene_buffer *buffer_node = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *surface_node = wlr_scene_surface_try_from_buffer(buffer_node);
	if (!surface_node) {
		return NULL;
	}

	struct wlr_xwayland_surface *xwayland_surface =
		wlr_xwayland_surface_try_from_wlr_surface(surface_node->surface);
	if (!xwayland_surface || xwayland_surface->override_redirect) {
		return NULL;
	}

	return xwayland_surface;
}

static void restack_xwayland_surface(struct wlr_scene_node *node,
		struct wlr_box *box, struct scene_update_data *data) {
	struct wlr_xwayland_surface *xwayland_surface =
		scene_node_try_get_managed_xwayland_surface(node);
	if (!xwayland_surface) {
		return;
	}

	// ensure this node is entirely inside the update region. If not, we can't
	// restack this node since we're not considering the whole thing.
	if (wlr_box_contains_box(&data->update_box, box)) {
		if (data->restack_above) {
			wlr_xwayland_surface_restack(xwayland_surface, data->restack_above, XCB_STACK_MODE_BELOW);
		} else {
			wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
		}
	}

	data->restack_above = xwayland_surface;
}

static void restack_xwayland_surface_below(struct wlr_scene_node *node) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			restack_xwayland_surface_below(child);
		}
		return;
	}

	struct wlr_xwayland_surface *xwayland_surface =
		scene_node_try_get_managed_xwayland_surface(node);
	if (!xwayland_surface) {
		return;
	}

	wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_BELOW);
}
#endif

static bool scene_node_update_iterator(struct wlr_scene_node *node,
		int lx, int ly, void *_data) {
	struct scene_update_data *data = _data;

	if (node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR) {
		struct wlr_scene_optimized_blur *scene_blur = wlr_scene_optimized_blur_from_node(node);
		if (scene_blur->dirty) {
			data->optimized_blur_dirty = true;
			// Restore the visible region back to default for the nodes below
			// the optimized blur node
			pixman_region32_clear(data->visible);
			pixman_region32_copy(data->visible, data->update_region);
		}
	}

	struct wlr_box box = { .x = lx, .y = ly };
	scene_node_get_size(node, &box.width, &box.height);

	pixman_region32_subtract(&node->visible, &node->visible, data->update_region);
	pixman_region32_union(&node->visible, &node->visible, data->visible);
	pixman_region32_intersect_rect(&node->visible, &node->visible,
		lx, ly, box.width, box.height);

	// Expand the damage to compensate for blur artifacts
	if (node->type == WLR_SCENE_NODE_BLUR) {
		wlr_region_expand(&node->visible, &node->visible,
			blur_data_calc_size(data->blur_data));
	}

	if (data->calculate_visibility && !data->optimized_blur_dirty) {
		pixman_region32_t opaque;
		pixman_region32_init(&opaque);
		scene_node_opaque_region(node, lx, ly, &opaque);
		pixman_region32_subtract(data->visible, data->visible, &opaque);
		pixman_region32_fini(&opaque);
	}

	update_node_update_outputs(node, data->outputs, NULL, NULL);
#if WLR_HAS_XWAYLAND
	restack_xwayland_surface(node, &box, data);
#endif

	return false;
}

static void scene_node_visibility(struct wlr_scene_node *node,
		pixman_region32_t *visible) {
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_visibility(child, visible);
		}
		return;
	}

	pixman_region32_union(visible, visible, &node->visible);
}

static void scene_node_bounds(struct wlr_scene_node *node,
		int x, int y, pixman_region32_t *visible) {
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_bounds(child, x + child->x, y + child->y, visible);
		}
		return;
	}

	int width, height;
	scene_node_get_size(node, &width, &height);
	pixman_region32_union_rect(visible, visible, x, y, width, height);
}

static void scene_update_region(struct wlr_scene *scene,
		pixman_region32_t *update_region) {
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	pixman_region32_copy(&visible, update_region);

	struct pixman_box32 *region_box = pixman_region32_extents(update_region);
	struct scene_update_data data = {
		.visible = &visible,
		.update_region = update_region,
		.update_box = {
			.x = region_box->x1,
			.y = region_box->y1,
			.width = region_box->x2 - region_box->x1,
			.height = region_box->y2 - region_box->y1,
		},
		.outputs = &scene->outputs,
		.calculate_visibility = scene->calculate_visibility,
		.optimized_blur_dirty = false,
		.blur_data = &scene->blur_data,
	};

	// update node visibility and output enter/leave events
	scene_nodes_in_box(&scene->tree.node, &data.update_box, scene_node_update_iterator, &data);

	pixman_region32_fini(&visible);
}

static void scene_node_update(struct wlr_scene_node *node,
		pixman_region32_t *damage) {
	struct wlr_scene *scene = scene_node_get_root(node);

	int x, y;
	if (!wlr_scene_node_coords(node, &x, &y)) {
#if WLR_HAS_XWAYLAND
		restack_xwayland_surface_below(node);
#endif
		if (damage) {
			scene_update_region(scene, damage);
			scene_damage_outputs(scene, damage);
			pixman_region32_fini(damage);
		}

		return;
	}

	pixman_region32_t visible;
	if (!damage) {
		pixman_region32_init(&visible);
		scene_node_visibility(node, &visible);
		damage = &visible;
	}

	pixman_region32_t update_region;
	pixman_region32_init(&update_region);
	pixman_region32_copy(&update_region, damage);
	scene_node_bounds(node, x, y, &update_region);

	scene_update_region(scene, &update_region);
	pixman_region32_fini(&update_region);

	scene_node_visibility(node, damage);
	scene_damage_outputs(scene, damage);
	pixman_region32_fini(damage);
}

struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_tree *parent,
		int width, int height, const float color[static 4]) {
	assert(parent);
	assert(width >= 0 && height >= 0);

	struct wlr_scene_rect *scene_rect = calloc(1, sizeof(*scene_rect));
	if (scene_rect == NULL) {
		return NULL;
	}
	scene_node_init(&scene_rect->node, WLR_SCENE_NODE_RECT, parent);

	scene_rect->width = width;
	scene_rect->height = height;
	memcpy(scene_rect->color, color, sizeof(scene_rect->color));
	scene_rect->corners = (struct fx_corner_radii){0};
	scene_rect->accepts_input = true;
	scene_rect->clipped_region = clipped_region_get_default();

	scene_node_update(&scene_rect->node, NULL);

	return scene_rect;
}

void wlr_scene_rect_set_size(struct wlr_scene_rect *rect, int width, int height) {
	if (rect->width == width && rect->height == height) {
		return;
	}

	assert(width >= 0 && height >= 0);

	rect->width = width;
	rect->height = height;
	scene_node_update(&rect->node, NULL);
}

void wlr_scene_rect_set_color(struct wlr_scene_rect *rect, const float color[static 4]) {
	if (memcmp(rect->color, color, sizeof(rect->color)) == 0) {
		return;
	}

	memcpy(rect->color, color, sizeof(rect->color));
	scene_node_update(&rect->node, NULL);
}

static void scene_buffer_handle_buffer_release(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_buffer *scene_buffer =
		wl_container_of(listener, scene_buffer, buffer_release);

	scene_buffer->buffer = NULL;
	wl_list_remove(&scene_buffer->buffer_release.link);
	wl_list_init(&scene_buffer->buffer_release.link);
}

static void scene_buffer_set_buffer(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer) {
	wl_list_remove(&scene_buffer->buffer_release.link);
	wl_list_init(&scene_buffer->buffer_release.link);
	if (scene_buffer->own_buffer) {
		wlr_buffer_unlock(scene_buffer->buffer);
	}
	scene_buffer->buffer = NULL;
	scene_buffer->own_buffer = false;
	scene_buffer->buffer_width = scene_buffer->buffer_height = 0;
	scene_buffer->buffer_is_opaque = false;

	if (!buffer) {
		return;
	}

	scene_buffer->own_buffer = true;
	scene_buffer->buffer = wlr_buffer_lock(buffer);
	scene_buffer->buffer_width = buffer->width;
	scene_buffer->buffer_height = buffer->height;
	scene_buffer->buffer_is_opaque = wlr_buffer_is_opaque(buffer);

	scene_buffer->buffer_release.notify = scene_buffer_handle_buffer_release;
	wl_signal_add(&buffer->events.release, &scene_buffer->buffer_release);
}

static void scene_buffer_handle_renderer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_buffer *scene_buffer = wl_container_of(listener, scene_buffer, renderer_destroy);
	scene_buffer_set_texture(scene_buffer, NULL);
}

static void scene_buffer_set_texture(struct wlr_scene_buffer *scene_buffer,
		struct wlr_texture *texture) {
	wl_list_remove(&scene_buffer->renderer_destroy.link);
	wlr_texture_destroy(scene_buffer->texture);
	scene_buffer->texture = texture;

	if (texture != NULL) {
		scene_buffer->renderer_destroy.notify = scene_buffer_handle_renderer_destroy;
		wl_signal_add(&texture->renderer->events.destroy, &scene_buffer->renderer_destroy);
	} else {
		wl_list_init(&scene_buffer->renderer_destroy.link);
	}
}

static void scene_buffer_set_wait_timeline(struct wlr_scene_buffer *scene_buffer,
		struct wlr_drm_syncobj_timeline *timeline, uint64_t point) {
	wlr_drm_syncobj_timeline_unref(scene_buffer->wait_timeline);
	if (timeline != NULL) {
		scene_buffer->wait_timeline = wlr_drm_syncobj_timeline_ref(timeline);
		scene_buffer->wait_point = point;
	} else {
		scene_buffer->wait_timeline = NULL;
		scene_buffer->wait_point = 0;
	}
}

inline void wlr_scene_rect_set_corner_radius(struct wlr_scene_rect *rect, int corner_radius) {
	wlr_scene_rect_set_corner_radii(rect, corner_radii_all(corner_radius));
}

void wlr_scene_rect_set_corner_radii(struct wlr_scene_rect *rect, struct fx_corner_radii corners) {
	if (fx_corner_radii_eq(rect->corners, corners)) {
		return;
	}

	rect->corners = corners;
	scene_node_update(&rect->node, NULL);
}

void wlr_scene_rect_set_clipped_region(struct wlr_scene_rect *rect,
		struct clipped_region clipped_region) {
	if (fx_corner_radii_eq(rect->clipped_region.corners, clipped_region.corners) &&
			wlr_box_equal(&rect->clipped_region.area, &clipped_region.area)) {
		return;
	}

	rect->clipped_region = clipped_region;
	scene_node_update(&rect->node, NULL);
}

/*
 * Box Shadow
 */

struct wlr_scene_shadow *wlr_scene_shadow_create(struct wlr_scene_tree *parent,
		int width, int height, int corner_radius, float blur_sigma,
		const float color [static 4]) {
	struct wlr_scene_shadow *scene_shadow = calloc(1, sizeof(*scene_shadow));
	if (scene_shadow == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_shadow->node, WLR_SCENE_NODE_SHADOW, parent);

	scene_shadow->width = width;
	scene_shadow->height = height;
	scene_shadow->corner_radius = corner_radius;
	scene_shadow->blur_sigma = blur_sigma;
	memcpy(scene_shadow->color, color, sizeof(scene_shadow->color));
	scene_shadow->clipped_region = clipped_region_get_default();

	scene_node_update(&scene_shadow->node, NULL);

	return scene_shadow;
}

void wlr_scene_shadow_set_size(struct wlr_scene_shadow *shadow, int width, int height) {
	if (shadow->width == width && shadow->height == height) {
		return;
	}

	shadow->width = width;
	shadow->height = height;
	scene_node_update(&shadow->node, NULL);
}

void wlr_scene_shadow_set_corner_radius(struct wlr_scene_shadow *shadow, int corner_radius) {
	if (shadow->corner_radius == corner_radius) {
		return;
	}

	shadow->corner_radius = corner_radius;
	scene_node_update(&shadow->node, NULL);
}

void wlr_scene_shadow_set_blur_sigma(struct wlr_scene_shadow *shadow, float blur_sigma) {
	if (shadow->blur_sigma == blur_sigma) {
		return;
	}

	shadow->blur_sigma = blur_sigma;
	scene_node_update(&shadow->node, NULL);
}

void wlr_scene_shadow_set_color(struct wlr_scene_shadow *shadow, const float color[static 4]) {
	if (memcmp(shadow->color, color, sizeof(shadow->color)) == 0) {
		return;
	}

	memcpy(shadow->color, color, sizeof(shadow->color));
	scene_node_update(&shadow->node, NULL);
}

void wlr_scene_shadow_set_clipped_region(struct wlr_scene_shadow *shadow,
		struct clipped_region clipped_region) {
	if (fx_corner_radii_eq(shadow->clipped_region.corners, clipped_region.corners) &&
			wlr_box_equal(&shadow->clipped_region.area, &clipped_region.area)) {
		return;
	}

	shadow->clipped_region = clipped_region;
	scene_node_update(&shadow->node, NULL);
}

/*
 * Drop Shadow
 */

struct wlr_scene_drop_shadow *wlr_scene_drop_shadow_create(struct wlr_scene_tree *parent,
		int width, int height, float blur_sigma, const float color [static 4]) {
	struct wlr_scene_drop_shadow *drop_shadow = calloc(1, sizeof(*drop_shadow));
	if (drop_shadow == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&drop_shadow->node, WLR_SCENE_NODE_DROP_SHADOW, parent);

	drop_shadow->width = width;
	drop_shadow->height = height;
	drop_shadow->blur_sigma = blur_sigma;
	drop_shadow->blur_sample_size = wlr_scene_drop_shadow_calculate_offset(blur_sigma);
	memcpy(drop_shadow->color, color, sizeof(drop_shadow->color));

	drop_shadow->buffer_source = linked_node_init();

	scene_node_update(&drop_shadow->node, NULL);

	return drop_shadow;
}

void wlr_scene_drop_shadow_set_size(struct wlr_scene_drop_shadow *drop_shadow, int width, int height) {
	if (drop_shadow->width == width && drop_shadow->height == height) {
		return;
	}

	drop_shadow->width = width;
	drop_shadow->height = height;
	scene_node_update(&drop_shadow->node, NULL);
}

void wlr_scene_drop_shadow_set_blur_sigma(struct wlr_scene_drop_shadow *drop_shadow, float blur_sigma) {
	if (drop_shadow->blur_sigma == blur_sigma || blur_sigma < 0) {
		return;
	}

	drop_shadow->blur_sigma = blur_sigma;
	drop_shadow->blur_sample_size = wlr_scene_drop_shadow_calculate_offset(blur_sigma);
	scene_node_update(&drop_shadow->node, NULL);
}

void wlr_scene_drop_shadow_set_color(struct wlr_scene_drop_shadow *drop_shadow, const float color[static 4]) {
	if (memcmp(drop_shadow->color, color, sizeof(drop_shadow->color)) == 0) {
		return;
	}

	memcpy(drop_shadow->color, color, sizeof(drop_shadow->color));
	scene_node_update(&drop_shadow->node, NULL);
}

void wlr_scene_drop_shadow_set_reference_buffer(struct wlr_scene_drop_shadow *drop_shadow,
		struct wlr_scene_buffer *ref_buffer) {
	if (!ref_buffer && !drop_shadow->buffer_source.link) {
		return;
	}

	if (ref_buffer && linked_nodes_are_linked(&drop_shadow->buffer_source, &ref_buffer->drop_shadow)) {
		return;
	}

	linked_node_destroy(&drop_shadow->buffer_source);
	if (ref_buffer) {
		linked_node_destroy(&ref_buffer->drop_shadow);
		linked_node_init_link(&drop_shadow->buffer_source, &ref_buffer->drop_shadow);
	}

	scene_node_update(&drop_shadow->node, NULL);
}

struct wlr_scene_buffer *wlr_scene_drop_shadow_get_reference_buffer(struct wlr_scene_drop_shadow *drop_shadow) {
	struct linked_node *node = linked_nodes_get_sibling(&drop_shadow->buffer_source);
	if (node == NULL) {
		return NULL;
	}

	struct wlr_scene_buffer *scene_buffer = wl_container_of(node, scene_buffer, drop_shadow);
	return scene_buffer;
}

__always_inline int wlr_scene_drop_shadow_get_offset(struct wlr_scene_drop_shadow *drop_shadow) {
	return drop_shadow->blur_sample_size;
}

/*
 * Blur
 */

static void mark_all_optimized_blur_nodes_dirty(struct wlr_scene_node *node) {
	if (node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR) {
		struct wlr_scene_optimized_blur *scene_blur = wlr_scene_optimized_blur_from_node(node);
		wlr_scene_optimized_blur_mark_dirty(scene_blur);
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			mark_all_optimized_blur_nodes_dirty(child);
		}
	}
}

struct wlr_scene_blur *wlr_scene_blur_create(struct wlr_scene_tree *parent,
       int width, int height) {
	struct wlr_scene_blur *blur = calloc(1, sizeof(*blur));
	if (blur == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&blur->node, WLR_SCENE_NODE_BLUR, parent);

	blur->alpha = 1.0f;
	blur->strength = 1.0f;
	blur->clipped_region = (struct clipped_region){0};
	blur->corners = corner_radii_all(0);
	blur->should_only_blur_bottom_layer = false;
	blur->transparency_mask_source = linked_node_init();
	blur->width = width;
	blur->height = height;

	scene_node_update(&blur->node, NULL);

	return blur;
}

void wlr_scene_blur_set_size(struct wlr_scene_blur *blur, int width, int height) {
	if (blur->width == width && blur->height == height) {
		return;
	}

	blur->width = width;
	blur->height = height;

	scene_node_update(&blur->node, NULL);
}

inline void wlr_scene_blur_set_corner_radius(struct wlr_scene_blur *blur, int corner_radius) {
	wlr_scene_blur_set_corner_radii(blur, corner_radii_all(corner_radius));
}

void wlr_scene_blur_set_corner_radii(struct wlr_scene_blur *blur, struct fx_corner_radii corners) {
	if (fx_corner_radii_eq(blur->corners, corners)) {
		return;
	}

	blur->corners = corners;
	scene_node_update(&blur->node, NULL);
}

void wlr_scene_blur_set_should_only_blur_bottom_layer(struct wlr_scene_blur *blur,
	bool should_only_blur_bottom_layer) {
	if (blur->should_only_blur_bottom_layer == should_only_blur_bottom_layer) {
		return;
	}

	blur->should_only_blur_bottom_layer = should_only_blur_bottom_layer;
	scene_node_update(&blur->node, NULL);
}

void wlr_scene_blur_set_transparency_mask_source(struct wlr_scene_blur *blur,
       struct wlr_scene_buffer *source) {
	if (source == NULL && blur->transparency_mask_source.link == NULL) {
		return;
	}

	if (source != NULL && linked_nodes_are_linked(&blur->transparency_mask_source, &source->blur)) {
		return;
	}

	linked_node_destroy(&blur->transparency_mask_source);

	if (source != NULL) {
		linked_node_destroy(&source->blur);
		linked_node_init_link(&blur->transparency_mask_source, &source->blur);
	}

	scene_node_update(&blur->node, NULL);
}

struct wlr_scene_buffer *wlr_scene_blur_get_transparency_mask_source(
	struct wlr_scene_blur *blur) {
	struct linked_node *node = linked_nodes_get_sibling(&blur->transparency_mask_source);
	if (node == NULL) {
		return NULL;
	}

	struct wlr_scene_buffer *output = wl_container_of(node, output, blur);
	return output;
}

void wlr_scene_blur_set_alpha(struct wlr_scene_blur *blur, float alpha) {
	if (blur->alpha == alpha) {
		return;
	}

	blur->alpha = alpha;
	scene_node_update(&blur->node, NULL);
}

void wlr_scene_blur_set_strength(struct wlr_scene_blur *blur, float strength) {
	if (blur->strength == strength) {
		return;
	}

	blur->strength = strength;
	scene_node_update(&blur->node, NULL);
}

void wlr_scene_blur_set_clipped_region(struct wlr_scene_blur *blur,
		struct clipped_region clipped_region) {
	if (fx_corner_radii_eq(blur->clipped_region.corners, clipped_region.corners) &&
		wlr_box_equal(&blur->clipped_region.area, &clipped_region.area)) {
		return;
	}

	blur->clipped_region = clipped_region;
	scene_node_update(&blur->node, NULL);
}

void wlr_scene_set_blur_data(struct wlr_scene *scene, int num_passes,
		int radius, float noise, float brightness, float contrast, float saturation) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->num_passes == num_passes
			&& buff_data->radius == radius
			&& buff_data->noise == noise
			&& buff_data->brightness == brightness
			&& buff_data->contrast == contrast
			&& buff_data->saturation == saturation) {
		return;
	}

	buff_data->num_passes = num_passes;
	buff_data->radius = radius;
	buff_data->noise = noise;
	buff_data->brightness = brightness;
	buff_data->contrast = contrast;
	buff_data->saturation = saturation;

	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

void wlr_scene_set_blur_num_passes(struct wlr_scene *scene, int num_passes) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->num_passes == num_passes) {
		return;
	}
	buff_data->num_passes = num_passes;
	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

void wlr_scene_set_blur_radius(struct wlr_scene *scene, int radius) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->radius == radius) {
		return;
	}
	buff_data->radius = radius;
	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

void wlr_scene_set_blur_noise(struct wlr_scene *scene, float noise) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->noise == noise) {
		return;
	}
	buff_data->noise = noise;
	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

void wlr_scene_set_blur_brightness(struct wlr_scene *scene, float brightness) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->brightness == brightness) {
		return;
	}
	buff_data->brightness = brightness;
	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

void wlr_scene_set_blur_contrast(struct wlr_scene *scene, float contrast) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->contrast == contrast) {
		return;
	}
	buff_data->contrast = contrast;
	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

void wlr_scene_set_blur_saturation(struct wlr_scene *scene, float saturation) {
	struct blur_data *buff_data = &scene->blur_data;
	if (buff_data->saturation == saturation) {
		return;
	}
	buff_data->saturation = saturation;
	mark_all_optimized_blur_nodes_dirty(&scene->tree.node);
	scene_node_update(&scene->tree.node, NULL);
}

struct wlr_scene_optimized_blur *wlr_scene_optimized_blur_create(
		struct wlr_scene_tree *parent, int width, int height) {
	struct wlr_scene_optimized_blur *scene_blur = calloc(1, sizeof(*scene_blur));
	if (scene_blur == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_blur->node, WLR_SCENE_NODE_OPTIMIZED_BLUR, parent);

	scene_blur->width = width;
	scene_blur->height = height;
	scene_blur->dirty = false;

	scene_node_update(&scene_blur->node, NULL);

	return scene_blur;
}

void wlr_scene_optimized_blur_set_size(struct wlr_scene_optimized_blur *blur_node,
		int width, int height) {
	assert(blur_node);
	if (blur_node->width == width && blur_node->height == height) {
		return;
	}

	blur_node->width = width;
	blur_node->height = height;

	wlr_scene_optimized_blur_mark_dirty(blur_node);
}

void wlr_scene_optimized_blur_mark_dirty(struct wlr_scene_optimized_blur *blur_node) {
	// Skip re-rendering the optimized blur if the blur node is disabled
	if (blur_node && !blur_node->node.enabled) {
		return;
	}

	blur_node->dirty = true;

	scene_node_update(&blur_node->node, NULL);
}

struct wlr_scene_buffer *wlr_scene_buffer_create(struct wlr_scene_tree *parent,
		struct wlr_buffer *buffer) {
	struct wlr_scene_buffer *scene_buffer = calloc(1, sizeof(*scene_buffer));
	if (scene_buffer == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_buffer->node, WLR_SCENE_NODE_BUFFER, parent);

	wl_signal_init(&scene_buffer->events.outputs_update);
	wl_signal_init(&scene_buffer->events.output_enter);
	wl_signal_init(&scene_buffer->events.output_leave);
	wl_signal_init(&scene_buffer->events.output_sample);
	wl_signal_init(&scene_buffer->events.frame_done);

	pixman_region32_init(&scene_buffer->opaque_region);
	wl_list_init(&scene_buffer->buffer_release.link);
	wl_list_init(&scene_buffer->renderer_destroy.link);
	scene_buffer->opacity = 1;

	scene_buffer->corners = corner_radii_none();

	scene_buffer->drop_shadow = linked_node_init();
	scene_buffer->blur = linked_node_init();

	scene_buffer_set_buffer(scene_buffer, buffer);
	scene_node_update(&scene_buffer->node, NULL);

	return scene_buffer;
}

void wlr_scene_buffer_set_buffer_with_options(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer, const struct wlr_scene_buffer_set_buffer_options *options) {
	const struct wlr_scene_buffer_set_buffer_options default_options = {0};
	if (options == NULL) {
		options = &default_options;
	}

	// specifying a region for a NULL buffer doesn't make sense. We need to know
	// about the buffer to scale the buffer local coordinates down to scene
	// coordinates.
	assert(buffer || !options->damage);

	bool mapped = buffer != NULL;
	bool prev_mapped = scene_buffer->buffer != NULL || scene_buffer->texture != NULL;

	if (!mapped && !prev_mapped) {
		// unmapping already unmapped buffer - noop
		return;
	}

	// if this node used to not be mapped or its previous displayed
	// buffer region will be different from what the new buffer would
	// produce we need to update the node.
	bool update = mapped != prev_mapped;
	if (buffer != NULL && scene_buffer->dst_width == 0 && scene_buffer->dst_height == 0) {
		update = update || scene_buffer->buffer_width != buffer->width ||
			scene_buffer->buffer_height != buffer->height;
	}

	// If this is a buffer change, check if it's a single pixel buffer.
	// Cache that so we can still apply rendering optimisations even when
	// the original buffer has been freed after texture upload.
	if (buffer != scene_buffer->buffer) {
		scene_buffer->is_single_pixel_buffer = false;
		struct wlr_client_buffer *client_buffer = NULL;
		if (buffer != NULL) {
			client_buffer = wlr_client_buffer_get(buffer);
		}
		if (client_buffer != NULL && client_buffer->source != NULL) {
			struct wlr_single_pixel_buffer_v1 *single_pixel_buffer =
				wlr_single_pixel_buffer_v1_try_from_buffer(client_buffer->source);
			if (single_pixel_buffer != NULL) {
				scene_buffer->is_single_pixel_buffer = true;
				scene_buffer->single_pixel_buffer_color[0] = single_pixel_buffer->r;
				scene_buffer->single_pixel_buffer_color[1] = single_pixel_buffer->g;
				scene_buffer->single_pixel_buffer_color[2] = single_pixel_buffer->b;
				scene_buffer->single_pixel_buffer_color[3] = single_pixel_buffer->a;
			}
		}
	}

	scene_buffer_set_buffer(scene_buffer, buffer);
	scene_buffer_set_texture(scene_buffer, NULL);
	scene_buffer_set_wait_timeline(scene_buffer,
		options->wait_timeline, options->wait_point);

	if (update) {
		scene_node_update(&scene_buffer->node, NULL);
		// updating the node will already damage the whole node for us. Return
		// early to not damage again
		return;
	}

	int lx, ly;
	if (!wlr_scene_node_coords(&scene_buffer->node, &lx, &ly)) {
		return;
	}

	pixman_region32_t fallback_damage;
	pixman_region32_init_rect(&fallback_damage, 0, 0, buffer->width, buffer->height);
	const pixman_region32_t *damage = options->damage;
	if (!damage) {
		damage = &fallback_damage;
	}

	struct wlr_fbox box = scene_buffer->src_box;
	if (wlr_fbox_empty(&box)) {
		box.x = 0;
		box.y = 0;
		box.width = buffer->width;
		box.height = buffer->height;
	}

	wlr_fbox_transform(&box, &box, scene_buffer->transform,
		buffer->width, buffer->height);

	float scale_x, scale_y;
	if (scene_buffer->dst_width || scene_buffer->dst_height) {
		scale_x = scene_buffer->dst_width / box.width;
		scale_y = scene_buffer->dst_height / box.height;
	} else {
		scale_x = buffer->width / box.width;
		scale_y = buffer->height / box.height;
	}

	pixman_region32_t trans_damage;
	pixman_region32_init(&trans_damage);
	wlr_region_transform(&trans_damage, damage,
		scene_buffer->transform, buffer->width, buffer->height);
	pixman_region32_intersect_rect(&trans_damage, &trans_damage,
		box.x, box.y, box.width, box.height);
	pixman_region32_translate(&trans_damage, -box.x, -box.y);

	struct wlr_scene *scene = scene_node_get_root(&scene_buffer->node);
	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		float output_scale = scene_output->output->scale;
		float output_scale_x = output_scale * scale_x;
		float output_scale_y = output_scale * scale_y;
		pixman_region32_t output_damage;
		pixman_region32_init(&output_damage);
		wlr_region_scale_xy(&output_damage, &trans_damage,
			output_scale_x, output_scale_y);

		// One output pixel will match (buffer_scale_x)x(buffer_scale_y) buffer pixels.
		// If the buffer is upscaled on the given axis (output_scale_* > 1.0,
		// buffer_scale_* < 1.0), its contents will bleed into adjacent
		// (ceil(output_scale_* / 2)) output pixels because of linear filtering.
		// Additionally, if the buffer is downscaled (output_scale_* < 1.0,
		// buffer_scale_* > 1.0), and one output pixel matches a non-integer number of
		// buffer pixels, its contents will bleed into neighboring output pixels.
		// Handle both cases by computing buffer_scale_{x,y} and checking if they are
		// integer numbers; ceilf() is used to ensure that the distance is at least 1.
		float buffer_scale_x = 1.0f / output_scale_x;
		float buffer_scale_y = 1.0f / output_scale_y;
		int dist_x = floor(buffer_scale_x) != buffer_scale_x ?
			(int)ceilf(output_scale_x / 2.0f) : 0;
		int dist_y = floor(buffer_scale_y) != buffer_scale_y ?
			(int)ceilf(output_scale_y / 2.0f) : 0;
		// TODO: expand with per-axis distances
		wlr_region_expand(&output_damage, &output_damage,
			dist_x >= dist_y ? dist_x : dist_y);

		pixman_region32_t cull_region;
		pixman_region32_init(&cull_region);
		pixman_region32_copy(&cull_region, &scene_buffer->node.visible);
		scale_region(&cull_region, output_scale, true);
		pixman_region32_translate(&cull_region, -lx * output_scale, -ly * output_scale);
		pixman_region32_intersect(&output_damage, &output_damage, &cull_region);
		pixman_region32_fini(&cull_region);

		// Damage the linked drop-shadow node. Makes sure that the linked
		// shadow-node gets updated when it's reference buffer get damaged.
		// Copies the damaged region, translates it to the linked nodes
		// position, and adds it to the original output damage.
		struct linked_node *shadow_link = linked_nodes_get_sibling(&scene_buffer->drop_shadow);
		if (shadow_link) {
			struct wlr_scene_drop_shadow *drop_shadow
				= wl_container_of(shadow_link, drop_shadow, buffer_source);
			int node_lx, node_ly;
			if (drop_shadow && drop_shadow->blur_sigma > 0.0f
					&& drop_shadow->color[3] > 0.0f
					&& wlr_scene_node_coords(&drop_shadow->node, &node_lx, &node_ly)) {
				pixman_region32_t shadow_damage;
				pixman_region32_init(&shadow_damage);
				pixman_region32_copy(&shadow_damage, &output_damage);
				pixman_region32_translate(&shadow_damage,
						((node_lx + drop_shadow->blur_sample_size) - lx) * output_scale,
						((node_ly + drop_shadow->blur_sample_size) - ly) * output_scale);
				wlr_region_expand(&shadow_damage, &shadow_damage,
						drop_shadow->blur_sample_size);
				pixman_region32_union(&output_damage, &output_damage, &shadow_damage);
				pixman_region32_fini(&shadow_damage);
			}
		}

		pixman_region32_translate(&output_damage,
			(int)round((lx - scene_output->x) * output_scale),
			(int)round((ly - scene_output->y) * output_scale));
		output_to_buffer_coords(&output_damage, scene_output->output);
		scene_output_damage(scene_output, &output_damage);
		pixman_region32_fini(&output_damage);
	}

	pixman_region32_fini(&trans_damage);
	pixman_region32_fini(&fallback_damage);
}

void wlr_scene_buffer_set_buffer_with_damage(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer, const pixman_region32_t *damage) {
	const struct wlr_scene_buffer_set_buffer_options options = {
		.damage = damage,
	};
	wlr_scene_buffer_set_buffer_with_options(scene_buffer, buffer, &options);
}

void wlr_scene_buffer_set_buffer(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer)  {
	wlr_scene_buffer_set_buffer_with_options(scene_buffer, buffer, NULL);
}

void wlr_scene_buffer_set_opaque_region(struct wlr_scene_buffer *scene_buffer,
		const pixman_region32_t *region) {
	if (pixman_region32_equal(&scene_buffer->opaque_region, region)) {
		return;
	}

	pixman_region32_copy(&scene_buffer->opaque_region, region);

	int x, y;
	if (!wlr_scene_node_coords(&scene_buffer->node, &x, &y)) {
		return;
	}

	pixman_region32_t update_region;
	pixman_region32_init(&update_region);
	scene_node_bounds(&scene_buffer->node, x, y, &update_region);
	scene_update_region(scene_node_get_root(&scene_buffer->node), &update_region);
	pixman_region32_fini(&update_region);
}

void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *scene_buffer,
		const struct wlr_fbox *box) {
	if (wlr_fbox_equal(&scene_buffer->src_box, box)) {
		return;
	}

	if (box != NULL) {
		assert(box->x >= 0 && box->y >= 0 && box->width >= 0 && box->height >= 0);
		scene_buffer->src_box = *box;
	} else {
		scene_buffer->src_box = (struct wlr_fbox){0};
	}

	scene_node_update(&scene_buffer->node, NULL);
}

void wlr_scene_buffer_set_dest_size(struct wlr_scene_buffer *scene_buffer,
		int width, int height) {
	if (scene_buffer->dst_width == width && scene_buffer->dst_height == height) {
		return;
	}

	assert(width >= 0 && height >= 0);
	scene_buffer->dst_width = width;
	scene_buffer->dst_height = height;
	scene_node_update(&scene_buffer->node, NULL);
}

void wlr_scene_buffer_set_transform(struct wlr_scene_buffer *scene_buffer,
		enum wl_output_transform transform) {
	if (scene_buffer->transform == transform) {
		return;
	}

	scene_buffer->transform = transform;
	scene_node_update(&scene_buffer->node, NULL);
}

void wlr_scene_buffer_send_frame_done(struct wlr_scene_buffer *scene_buffer,
		struct timespec *now) {
	if (!pixman_region32_empty(&scene_buffer->node.visible)) {
		wl_signal_emit_mutable(&scene_buffer->events.frame_done, now);
	}
}

void wlr_scene_buffer_set_opacity(struct wlr_scene_buffer *scene_buffer,
		float opacity) {
	if (scene_buffer->opacity == opacity) {
		return;
	}

	assert(opacity >= 0 && opacity <= 1);
	scene_buffer->opacity = opacity;
	scene_node_update(&scene_buffer->node, NULL);
}

void wlr_scene_buffer_set_filter_mode(struct wlr_scene_buffer *scene_buffer,
		enum wlr_scale_filter_mode filter_mode) {
	if (scene_buffer->filter_mode == filter_mode) {
		return;
	}

	scene_buffer->filter_mode = filter_mode;
	scene_node_update(&scene_buffer->node, NULL);
}

inline void wlr_scene_buffer_set_corner_radius(struct wlr_scene_buffer *scene_buffer,
		int radii) {
	wlr_scene_buffer_set_corner_radii(scene_buffer, corner_radii_all(radii));
}

void wlr_scene_buffer_set_corner_radii(struct wlr_scene_buffer *scene_buffer,
	struct fx_corner_radii corner_radii) {
	if (fx_corner_radii_eq(scene_buffer->corners, corner_radii)) {
		return;
	}

	scene_buffer->corners = corner_radii;
	scene_node_update(&scene_buffer->node, NULL);
}

static struct wlr_texture *scene_buffer_get_texture(
		struct wlr_scene_buffer *scene_buffer, struct wlr_renderer *renderer) {
	if (scene_buffer->buffer == NULL || scene_buffer->texture != NULL) {
		return scene_buffer->texture;
	}

	struct wlr_client_buffer *client_buffer =
		wlr_client_buffer_get(scene_buffer->buffer);
	if (client_buffer != NULL) {
		return client_buffer->texture;
	}

	struct wlr_texture *texture =
		wlr_texture_from_buffer(renderer, scene_buffer->buffer);
	if (texture != NULL && scene_buffer->own_buffer) {
		scene_buffer->own_buffer = false;
		wlr_buffer_unlock(scene_buffer->buffer);
	}
	scene_buffer_set_texture(scene_buffer, texture);
	return texture;
}

static void scene_node_get_size(struct wlr_scene_node *node,
		int *width, int *height) {
	*width = 0;
	*height = 0;

	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		return;
	case WLR_SCENE_NODE_OPTIMIZED_BLUR:;
		struct wlr_scene_optimized_blur *scene_blur =
			wlr_scene_optimized_blur_from_node(node);
		*width = scene_blur->width;
		*height = scene_blur->height;
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);
		*width = scene_rect->width;
		*height = scene_rect->height;
		break;
	case WLR_SCENE_NODE_SHADOW:;
		struct wlr_scene_shadow *scene_shadow = wlr_scene_shadow_from_node(node);
		*width = scene_shadow->width;
		*height = scene_shadow->height;
		break;
	case WLR_SCENE_NODE_DROP_SHADOW:;
		struct wlr_scene_drop_shadow *drop_shadow = wlr_scene_drop_shadow_from_node(node);
		*width = drop_shadow->width;
		*height = drop_shadow->height;
		break;
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		if (scene_buffer->dst_width > 0 && scene_buffer->dst_height > 0) {
			*width = scene_buffer->dst_width;
			*height = scene_buffer->dst_height;
		} else {
			*width = scene_buffer->buffer_width;
			*height = scene_buffer->buffer_height;
			wlr_output_transform_coords(scene_buffer->transform, width, height);
		}
		break;
	case WLR_SCENE_NODE_BLUR:;
		struct wlr_scene_blur *blur = wlr_scene_blur_from_node(node);
		*width = blur->width;
		*height = blur->height;
		break;
	}
}

void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled) {
	if (node->enabled == enabled) {
		return;
	}

	int x, y;
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	if (wlr_scene_node_coords(node, &x, &y)) {
		scene_node_visibility(node, &visible);
	}

	node->enabled = enabled;

	scene_node_update(node, &visible);
}

void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y) {
	if (node->x == x && node->y == y) {
		return;
	}

	node->x = x;
	node->y = y;
	scene_node_update(node, NULL);
}

void wlr_scene_node_place_above(struct wlr_scene_node *node,
		struct wlr_scene_node *sibling) {
	assert(node != sibling);
	assert(node->parent == sibling->parent);

	if (node->link.prev == &sibling->link) {
		return;
	}

	wl_list_remove(&node->link);
	wl_list_insert(&sibling->link, &node->link);
	scene_node_update(node, NULL);
}

void wlr_scene_node_place_below(struct wlr_scene_node *node,
		struct wlr_scene_node *sibling) {
	assert(node != sibling);
	assert(node->parent == sibling->parent);

	if (node->link.next == &sibling->link) {
		return;
	}

	wl_list_remove(&node->link);
	wl_list_insert(sibling->link.prev, &node->link);
	scene_node_update(node, NULL);
}

void wlr_scene_node_raise_to_top(struct wlr_scene_node *node) {
	struct wlr_scene_node *current_top = wl_container_of(
		node->parent->children.prev, current_top, link);
	if (node == current_top) {
		return;
	}
	wlr_scene_node_place_above(node, current_top);
}

void wlr_scene_node_lower_to_bottom(struct wlr_scene_node *node) {
	struct wlr_scene_node *current_bottom = wl_container_of(
		node->parent->children.next, current_bottom, link);
	if (node == current_bottom) {
		return;
	}
	wlr_scene_node_place_below(node, current_bottom);
}

void wlr_scene_node_reparent(struct wlr_scene_node *node,
		struct wlr_scene_tree *new_parent) {
	assert(new_parent != NULL);

	if (node->parent == new_parent) {
		return;
	}

	/* Ensure that a node cannot become its own ancestor */
	for (struct wlr_scene_tree *ancestor = new_parent; ancestor != NULL;
			ancestor = ancestor->node.parent) {
		assert(&ancestor->node != node);
	}

	int x, y;
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	if (wlr_scene_node_coords(node, &x, &y)) {
		scene_node_visibility(node, &visible);
	}

	wl_list_remove(&node->link);
	node->parent = new_parent;
	wl_list_insert(new_parent->children.prev, &node->link);
	scene_node_update(node, &visible);
}

bool wlr_scene_node_coords(struct wlr_scene_node *node,
		int *lx_ptr, int *ly_ptr) {
	assert(node);

	int lx = 0, ly = 0;
	bool enabled = true;
	while (true) {
		lx += node->x;
		ly += node->y;
		enabled = enabled && node->enabled;
		if (node->parent == NULL) {
			break;
		}

		node = &node->parent->node;
	}

	*lx_ptr = lx;
	*ly_ptr = ly;
	return enabled;
}

static void scene_node_for_each_scene_buffer(struct wlr_scene_node *node,
		int lx, int ly, wlr_scene_buffer_iterator_func_t user_iterator,
		void *user_data) {
	if (!node->enabled) {
		return;
	}

	lx += node->x;
	ly += node->y;

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		user_iterator(scene_buffer, lx, ly, user_data);
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_for_each_scene_buffer(child, lx, ly, user_iterator, user_data);
		}
	}
}

void wlr_scene_node_for_each_buffer(struct wlr_scene_node *node,
		wlr_scene_buffer_iterator_func_t user_iterator, void *user_data) {
	scene_node_for_each_scene_buffer(node, 0, 0, user_iterator, user_data);
}

struct node_at_data {
	double lx, ly;
	double rx, ry;
	struct wlr_scene_node *node;
};

static bool scene_node_at_iterator(struct wlr_scene_node *node,
		int lx, int ly, void *data) {
	struct node_at_data *at_data = data;

	double rx = at_data->lx - lx;
	double ry = at_data->ly - ly;

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		if (scene_buffer->point_accepts_input &&
				!scene_buffer->point_accepts_input(scene_buffer, &rx, &ry)) {
			return false;
		}
	} else if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
		if (!rect->accepts_input) {
			return false;
		} else if (!wlr_box_empty(&rect->clipped_region.area)
				&& wlr_box_contains_point(&rect->clipped_region.area, rx, ry)) {
			// Inside clipped region
			return false;
		}
	} else if (node->type == WLR_SCENE_NODE_SHADOW
			|| node->type == WLR_SCENE_NODE_DROP_SHADOW
			|| node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR
			|| node->type == WLR_SCENE_NODE_BLUR) {
		// Disable interaction
		return false;
	}

	at_data->rx = rx;
	at_data->ry = ry;
	at_data->node = node;
	return true;
}

struct wlr_scene_node *wlr_scene_node_at(struct wlr_scene_node *node,
		double lx, double ly, double *nx, double *ny) {
	struct wlr_box box = {
		.x = floor(lx),
		.y = floor(ly),
		.width = 1,
		.height = 1
	};

	struct node_at_data data = {
		.lx = lx,
		.ly = ly
	};

	if (scene_nodes_in_box(node, &box, scene_node_at_iterator, &data)) {
		if (nx) {
			*nx = data.rx;
		}
		if (ny) {
			*ny = data.ry;
		}
		return data.node;
	}

	return NULL;
}

struct render_list_entry {
	struct wlr_scene_node *node;
	bool highlight_transparent_region;
	int x, y;
};

static void scene_entry_render(struct render_list_entry *entry, const struct render_data *data) {
	struct wlr_scene_node *node = entry->node;

	pixman_region32_t render_region;
	pixman_region32_init(&render_region);
	pixman_region32_copy(&render_region, &node->visible);
	pixman_region32_translate(&render_region, -data->logical.x, -data->logical.y);
	logical_to_buffer_coords(&render_region, data, true);
	pixman_region32_intersect(&render_region, &render_region, &data->damage);
	if (pixman_region32_empty(&render_region)) {
		pixman_region32_fini(&render_region);
		return;
	}

	int x = entry->x - data->logical.x;
	int y = entry->y - data->logical.y;

	struct wlr_box dst_box = {
		.x = x,
		.y = y,
	};
	scene_node_get_size(node, &dst_box.width, &dst_box.height);
	transform_output_box(&dst_box, data);

	pixman_region32_t opaque;
	pixman_region32_init(&opaque);
	scene_node_opaque_region(node, x, y, &opaque);
	logical_to_buffer_coords(&opaque, data, false);
	pixman_region32_subtract(&opaque, &render_region, &opaque);

	enum wl_output_transform node_transform =
		wlr_output_transform_compose(WL_OUTPUT_TRANSFORM_NORMAL, data->transform);

	struct wlr_scene *scene = data->output->scene;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		assert(false);
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);
		struct fx_corner_radii rect_corners = scene_rect->corners;

		fx_corner_radii_transform(node_transform, &rect_corners);

		struct wlr_box rect_clipped_region_box = scene_rect->clipped_region.area;
		struct fx_corner_radii rect_clipped_corners = scene_rect->clipped_region.corners;

		// Node relative -> Root relative
		rect_clipped_region_box.x += x;
		rect_clipped_region_box.y += y;

		transform_output_box(&rect_clipped_region_box, data);
		fx_corner_radii_transform(node_transform, &rect_clipped_corners);

		struct fx_render_rect_options rect_options = {
			.base = {
				.box = dst_box,
				.color = {
					.r = scene_rect->color[0],
					.g = scene_rect->color[1],
					.b = scene_rect->color[2],
					.a = scene_rect->color[3],
				},
				.clip = &render_region,
			},
			.clipped_region = {
				.area = rect_clipped_region_box,
				.corners = fx_corner_radii_scale(rect_clipped_corners, data->scale),
			},
		};

		if (!fx_corner_radii_is_empty(&rect_corners)) {
			struct fx_render_rounded_rect_options rounded_rect_options = {
				.base = rect_options.base,
				.corners = fx_corner_radii_scale(rect_corners, data->scale),
				.clipped_region = rect_options.clipped_region,
			};
			fx_render_pass_add_rounded_rect(data->render_pass, &rounded_rect_options);
		} else {
			fx_render_pass_add_rect(data->render_pass, &rect_options);
		}
		break;
	case WLR_SCENE_NODE_OPTIMIZED_BLUR:;
		struct wlr_scene_optimized_blur *scene_blur = wlr_scene_optimized_blur_from_node(node);
		// Re-render the optimized blur buffer when needed
		if (data->has_blur && is_scene_blur_enabled(&scene->blur_data)
				&& scene_blur->dirty) {
			const float opacity = 1.0f;
			enum wl_output_transform transform =
				wlr_output_transform_invert(data->transform);
			transform = wlr_output_transform_compose(transform, data->transform);
			struct fx_render_blur_pass_options blur_options = {
				.tex_options = {
					.base = {
						.transform = transform,
						.alpha = &opacity,
						.blend_mode = WLR_RENDER_BLEND_MODE_NONE,
						.dst_box = dst_box,
					},
					.clip_box = &dst_box,
					.discard_transparent = false,
					.clipped_region = {0}
				},
				.blur_data = &scene->blur_data,
				.blur_strength = 1.0f,
			};
			bool result = fx_render_pass_add_optimized_blur(data->render_pass, &blur_options);
			if (result) {
				scene_blur->dirty = false;
			}
		}
		break;
	case WLR_SCENE_NODE_SHADOW: {
		struct wlr_scene_shadow *scene_shadow = wlr_scene_shadow_from_node(node);

		struct wlr_box shadow_clipped_region_box = scene_shadow->clipped_region.area;
		struct fx_corner_radii shadow_clipped_corners = scene_shadow->clipped_region.corners;

		// Node relative -> Root relative
		shadow_clipped_region_box.x += x;
		shadow_clipped_region_box.y += y;

		transform_output_box(&shadow_clipped_region_box, data);
		fx_corner_radii_transform(node_transform, &shadow_clipped_corners);

		struct fx_render_box_shadow_options shadow_options = {
			.box = dst_box,
			.clipped_region = {
				.area = shadow_clipped_region_box,
				.corners = fx_corner_radii_scale(shadow_clipped_corners, data->scale),
			},
			.blur_sigma = scene_shadow->blur_sigma,
			.corner_radius = scene_shadow->corner_radius * data->scale,
			.color = {
				.r = scene_shadow->color[0],
				.g = scene_shadow->color[1],
				.b = scene_shadow->color[2],
				.a = scene_shadow->color[3],
			},
			.clip = &render_region,
		};
		fx_render_pass_add_box_shadow(data->render_pass, &shadow_options);
		break;
	}
	case WLR_SCENE_NODE_DROP_SHADOW: {
		struct wlr_scene_drop_shadow *drop_shadow = wlr_scene_drop_shadow_from_node(node);

		struct wlr_scene_buffer *scene_buffer
			= wlr_scene_drop_shadow_get_reference_buffer(drop_shadow);
		if (!scene_buffer || !scene_buffer->node.enabled) {
			break;
		}

		struct wlr_texture *texture = scene_buffer_get_texture(scene_buffer,
				data->output->output->renderer);
		if (texture == NULL) {
			break;
		}

		const int shadow_size = drop_shadow->blur_sample_size * data->scale;

		// TODO: Fallback to regular box-shadow as an optimization when the
		// textures attributes indicate that it doesn't contain alpha or is
		// fully opaque?

		enum wl_output_transform transform =
			wlr_output_transform_invert(scene_buffer->transform);
		transform = wlr_output_transform_compose(transform, data->transform);
		struct fx_corner_radii buffer_corners = scene_buffer->corners;
		fx_corner_radii_transform(transform, &buffer_corners);

		dst_box.x += shadow_size;
		dst_box.y += shadow_size;
		dst_box.width -= shadow_size * 2;
		dst_box.height -= shadow_size * 2;

		const float opacity = 1.0f;
		struct fx_render_texture_options shadow_options = {
			// The same as the scene_buffer
			// TODO: function to get this instead instead of copy/pasting?
			.base = (struct wlr_render_texture_options){
				.texture = texture,
				.src_box = scene_buffer->src_box,
				.dst_box = dst_box,
				.transform = transform,
				.clip = &render_region, // Render with the smaller region, clipping CSD
				.alpha = &opacity,
				.filter_mode = scene_buffer->filter_mode,
				.blend_mode = !data->output->scene->calculate_visibility ||
					!pixman_region32_empty(&opaque) ?
					WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
				.wait_timeline = scene_buffer->wait_timeline,
				.wait_point = scene_buffer->wait_point,
			},
			.clip_box = NULL,
			.corners = fx_corner_radii_scale(buffer_corners, data->scale),
		};
		struct wlr_render_color color = {
			.r = drop_shadow->color[0],
			.g = drop_shadow->color[1],
			.b = drop_shadow->color[2],
			.a = drop_shadow->color[3],
		};
		fx_render_pass_add_drop_shadow(data->render_pass,
				&shadow_options, drop_shadow->blur_sigma, &color);
		break;
	}
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		struct fx_corner_radii buffer_corners = scene_buffer->corners;

		if (scene_buffer->is_single_pixel_buffer) {
			// TODO: Render blur/rounded corners/etc here:

			// Render the buffer as a rect, this is likely to be more efficient
			wlr_render_pass_add_rect(&data->render_pass->base, &(struct wlr_render_rect_options){
				.box = dst_box,
				.color = {
					.r = (float)scene_buffer->single_pixel_buffer_color[0] / (float)UINT32_MAX,
					.g = (float)scene_buffer->single_pixel_buffer_color[1] / (float)UINT32_MAX,
					.b = (float)scene_buffer->single_pixel_buffer_color[2] / (float)UINT32_MAX,
					.a = (float)scene_buffer->single_pixel_buffer_color[3] /
						(float)UINT32_MAX * scene_buffer->opacity,
				},
				.clip = &render_region,
			});
			break;
		}

		struct wlr_texture *texture = scene_buffer_get_texture(scene_buffer,
			data->output->output->renderer);
		if (texture == NULL) {
			scene_output_damage(data->output, &render_region);
			break;
		}

		enum wl_output_transform transform =
			wlr_output_transform_invert(scene_buffer->transform);
		transform = wlr_output_transform_compose(transform, data->transform);
		fx_corner_radii_transform(transform, &buffer_corners);

		// Base texture rendering options
		struct fx_render_texture_options tex_options = {
			.base = (struct wlr_render_texture_options){
				.texture = texture,
				.src_box = scene_buffer->src_box,
				.dst_box = dst_box,
				.transform = transform,
				.clip = &render_region, // Render with the smaller region, clipping CSD
				.alpha = &scene_buffer->opacity,
				.filter_mode = scene_buffer->filter_mode,
				.blend_mode = !data->output->scene->calculate_visibility ||
					!pixman_region32_empty(&opaque) ?
					WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
				.wait_timeline = scene_buffer->wait_timeline,
				.wait_point = scene_buffer->wait_point,
			},
			.clip_box = &dst_box,
			.corners = fx_corner_radii_scale(buffer_corners, data->scale),
			.clipped_region = {0},
		};

		fx_render_pass_add_texture(data->render_pass, &tex_options);

		struct wlr_scene_output_sample_event sample_event = {
			.output = data->output,
			.direct_scanout = false,
		};
		wl_signal_emit_mutable(&scene_buffer->events.output_sample, &sample_event);
		if (entry->highlight_transparent_region) {
			wlr_render_pass_add_rect(&data->render_pass->base, &(struct wlr_render_rect_options){
					.box = dst_box,
					.color = { .r = 0, .g = 0.3, .b = 0, .a = 0.3 },
					.clip = &opaque,
			});
		}
		break;
	case WLR_SCENE_NODE_BLUR:;
		struct wlr_scene_blur *blur = wlr_scene_blur_from_node(node);

		struct wlr_texture *tex = NULL;
		struct wlr_scene_buffer *mask = wlr_scene_blur_get_transparency_mask_source(blur);

		enum wl_output_transform mask_transform = WL_OUTPUT_TRANSFORM_NORMAL;

		struct wlr_fbox mask_src_box = {0};

		if (mask != NULL) {
			tex = scene_buffer_get_texture(mask, data->output->output->renderer);
			mask_transform = wlr_output_transform_invert(mask->transform);
			mask_transform = wlr_output_transform_compose(mask_transform, data->transform);
			mask_src_box = mask->src_box;
		}

		struct fx_corner_radii blur_corners = blur->corners;
		fx_corner_radii_transform(node_transform, &blur_corners);

		struct fx_render_blur_pass_options blur_options = {
			.tex_options = {
				.base = (struct wlr_render_texture_options) {
					.texture = tex,
					.src_box = mask_src_box,
					.dst_box = dst_box,
					.transform = mask_transform,
					.clip = &render_region,
					.alpha = &blur->alpha,
					.filter_mode = WLR_SCALE_FILTER_BILINEAR,
					.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
				},
				.clip_box = &dst_box,
				.corners = fx_corner_radii_scale(blur_corners, data->scale),
				.discard_transparent = false,
			},
			.use_optimized_blur = blur->should_only_blur_bottom_layer,
			.blur_data = &scene->blur_data,
			.ignore_transparent = mask != NULL,
			.blur_strength = blur->strength,
		};
		fx_render_pass_add_blur(data->render_pass, &blur_options);
	}

	pixman_region32_fini(&opaque);
	pixman_region32_fini(&render_region);
}

static void scene_handle_linux_dmabuf_v1_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene *scene =
		wl_container_of(listener, scene, linux_dmabuf_v1_destroy);
	wl_list_remove(&scene->linux_dmabuf_v1_destroy.link);
	wl_list_init(&scene->linux_dmabuf_v1_destroy.link);
	scene->linux_dmabuf_v1 = NULL;
}

void wlr_scene_set_linux_dmabuf_v1(struct wlr_scene *scene,
		struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1) {
	assert(scene->linux_dmabuf_v1 == NULL);
	scene->linux_dmabuf_v1 = linux_dmabuf_v1;
	scene->linux_dmabuf_v1_destroy.notify = scene_handle_linux_dmabuf_v1_destroy;
	wl_signal_add(&linux_dmabuf_v1->events.destroy, &scene->linux_dmabuf_v1_destroy);
}

static void scene_handle_gamma_control_manager_v1_set_gamma(struct wl_listener *listener,
		void *data) {
	const struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;
	struct wlr_scene *scene =
		wl_container_of(listener, scene, gamma_control_manager_v1_set_gamma);
	struct wlr_scene_output *output = wlr_scene_get_scene_output(scene, event->output);
	if (!output) {
		// this scene might not own this output.
		return;
	}

	output->gamma_lut_changed = true;
	output->gamma_lut = event->control;
	wlr_output_schedule_frame(output->output);
}

static void scene_handle_gamma_control_manager_v1_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene *scene =
		wl_container_of(listener, scene, gamma_control_manager_v1_destroy);
	wl_list_remove(&scene->gamma_control_manager_v1_destroy.link);
	wl_list_init(&scene->gamma_control_manager_v1_destroy.link);
	wl_list_remove(&scene->gamma_control_manager_v1_set_gamma.link);
	wl_list_init(&scene->gamma_control_manager_v1_set_gamma.link);
	scene->gamma_control_manager_v1 = NULL;

	struct wlr_scene_output *output;
	wl_list_for_each(output, &scene->outputs, link) {
		output->gamma_lut_changed = false;
		output->gamma_lut = NULL;
	}
}

void wlr_scene_set_gamma_control_manager_v1(struct wlr_scene *scene,
		struct wlr_gamma_control_manager_v1 *gamma_control) {
	assert(scene->gamma_control_manager_v1 == NULL);
	scene->gamma_control_manager_v1 = gamma_control;

	scene->gamma_control_manager_v1_destroy.notify =
		scene_handle_gamma_control_manager_v1_destroy;
	wl_signal_add(&gamma_control->events.destroy, &scene->gamma_control_manager_v1_destroy);
	scene->gamma_control_manager_v1_set_gamma.notify =
		scene_handle_gamma_control_manager_v1_set_gamma;
	wl_signal_add(&gamma_control->events.set_gamma, &scene->gamma_control_manager_v1_set_gamma);
}

static void scene_output_handle_destroy(struct wlr_addon *addon) {
	struct wlr_scene_output *scene_output =
		wl_container_of(addon, scene_output, addon);
	wlr_scene_output_destroy(scene_output);
}

static const struct wlr_addon_interface output_addon_impl = {
	.name = "wlr_scene_output",
	.destroy = scene_output_handle_destroy,
};

static void scene_node_output_update(struct wlr_scene_node *node,
		struct wl_list *outputs, struct wlr_scene_output *ignore,
		struct wlr_scene_output *force) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_output_update(child, outputs, ignore, force);
		}
		return;
	}

	update_node_update_outputs(node, outputs, ignore, force);
}

static void scene_output_update_geometry(struct wlr_scene_output *scene_output,
		bool force_update) {
	scene_output_damage_whole(scene_output);

	scene_node_output_update(&scene_output->scene->tree.node,
			&scene_output->scene->outputs, NULL, force_update ? scene_output : NULL);
}

static void scene_output_handle_commit(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_commit);
	struct wlr_output_event_commit *event = data;
	const struct wlr_output_state *state = event->state;

	// if the output has been committed with a certain damage, we know that region
	// will be acknowledged by the backend so we don't need to keep track of it
	// anymore
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		if (state->committed & WLR_OUTPUT_STATE_DAMAGE) {
			pixman_region32_subtract(&scene_output->pending_commit_damage,
					&scene_output->pending_commit_damage, &state->damage);
		} else {
			pixman_region32_fini(&scene_output->pending_commit_damage);
			pixman_region32_init(&scene_output->pending_commit_damage);
		}
	}

	bool force_update = state->committed & (
		WLR_OUTPUT_STATE_TRANSFORM |
		WLR_OUTPUT_STATE_SCALE |
		WLR_OUTPUT_STATE_SUBPIXEL);

	if (force_update || state->committed & (WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_ENABLED)) {
		scene_output_update_geometry(scene_output, force_update);
	}

	if (scene_output->scene->debug_damage_option == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT &&
			!wl_list_empty(&scene_output->damage_highlight_regions)) {
		wlr_output_schedule_frame(scene_output->output);
	}

	// Next time the output is enabled, try to re-apply the gamma LUT
	if (scene_output->scene->gamma_control_manager_v1 &&
			(state->committed & WLR_OUTPUT_STATE_ENABLED) &&
			!scene_output->output->enabled) {
		scene_output->gamma_lut_changed = true;
	}
}

static void scene_output_handle_damage(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_damage);
	struct wlr_output *output = scene_output->output;
	struct wlr_output_event_damage *event = data;

	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_copy(&damage, event->damage);
	wlr_region_transform(&damage, &damage,
		wlr_output_transform_invert(output->transform), width, height);
	scene_output_damage(scene_output, &damage);
	pixman_region32_fini(&damage);
}

static void scene_output_handle_needs_frame(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_needs_frame);
	wlr_output_schedule_frame(scene_output->output);
}

struct wlr_scene_output *wlr_scene_output_create(struct wlr_scene *scene,
		struct wlr_output *output) {
	struct wlr_scene_output *scene_output = calloc(1, sizeof(*scene_output));
	if (scene_output == NULL) {
		return NULL;
	}

	scene_output->output = output;
	scene_output->scene = scene;
	wlr_addon_init(&scene_output->addon, &output->addons, scene, &output_addon_impl);

	wlr_damage_ring_init(&scene_output->damage_ring);
	pixman_region32_init(&scene_output->pending_commit_damage);
	wl_list_init(&scene_output->damage_highlight_regions);

	int prev_output_index = -1;
	struct wl_list *prev_output_link = &scene->outputs;

	struct wlr_scene_output *current_output;
	wl_list_for_each(current_output, &scene->outputs, link) {
		if (prev_output_index + 1 != current_output->index) {
			break;
		}

		prev_output_index = current_output->index;
		prev_output_link = &current_output->link;
	}

	int drm_fd = wlr_backend_get_drm_fd(output->backend);
	if (drm_fd >= 0 && output->backend->features.timeline &&
			output->renderer != NULL && output->renderer->features.timeline) {
		scene_output->in_timeline = wlr_drm_syncobj_timeline_create(drm_fd);
		if (scene_output->in_timeline == NULL) {
			return NULL;
		}
	}

	scene_output->index = prev_output_index + 1;
	assert(scene_output->index < 64);
	wl_list_insert(prev_output_link, &scene_output->link);

	wl_signal_init(&scene_output->events.destroy);

	scene_output->output_commit.notify = scene_output_handle_commit;
	wl_signal_add(&output->events.commit, &scene_output->output_commit);

	scene_output->output_damage.notify = scene_output_handle_damage;
	wl_signal_add(&output->events.damage, &scene_output->output_damage);

	scene_output->output_needs_frame.notify = scene_output_handle_needs_frame;
	wl_signal_add(&output->events.needs_frame, &scene_output->output_needs_frame);

	scene_output_update_geometry(scene_output, false);

	return scene_output;
}

static void highlight_region_destroy(struct highlight_region *damage) {
	wl_list_remove(&damage->link);
	pixman_region32_fini(&damage->region);
	free(damage);
}

void wlr_scene_output_destroy(struct wlr_scene_output *scene_output) {
	if (scene_output == NULL) {
		return;
	}

	wl_signal_emit_mutable(&scene_output->events.destroy, NULL);

	scene_node_output_update(&scene_output->scene->tree.node,
		&scene_output->scene->outputs, scene_output, NULL);

	assert(wl_list_empty(&scene_output->events.destroy.listener_list));

	struct highlight_region *damage, *tmp_damage;
	wl_list_for_each_safe(damage, tmp_damage, &scene_output->damage_highlight_regions, link) {
		highlight_region_destroy(damage);
	}

	wlr_addon_finish(&scene_output->addon);
	wlr_damage_ring_finish(&scene_output->damage_ring);
	pixman_region32_fini(&scene_output->pending_commit_damage);
	wl_list_remove(&scene_output->link);
	wl_list_remove(&scene_output->output_commit.link);
	wl_list_remove(&scene_output->output_damage.link);
	wl_list_remove(&scene_output->output_needs_frame.link);
	wlr_drm_syncobj_timeline_unref(scene_output->in_timeline);
	wl_array_release(&scene_output->render_list);
	free(scene_output);
}

struct wlr_scene_output *wlr_scene_get_scene_output(struct wlr_scene *scene,
		struct wlr_output *output) {
	struct wlr_addon *addon =
		wlr_addon_find(&output->addons, scene, &output_addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct wlr_scene_output *scene_output =
		wl_container_of(addon, scene_output, addon);
	return scene_output;
}

void wlr_scene_output_set_position(struct wlr_scene_output *scene_output,
		int lx, int ly) {
	if (scene_output->x == lx && scene_output->y == ly) {
		return;
	}

	scene_output->x = lx;
	scene_output->y = ly;

	scene_output_update_geometry(scene_output, false);
}

static bool scene_node_invisible(struct wlr_scene_node *node) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		return true;
	} else if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);

		return rect->color[3] == 0.f;
	} else if (node->type == WLR_SCENE_NODE_SHADOW) {
		struct wlr_scene_shadow *shadow = wlr_scene_shadow_from_node(node);

		return shadow->color[3] == 0.f;
	} else if (node->type == WLR_SCENE_NODE_DROP_SHADOW) {
		struct wlr_scene_drop_shadow *drop_shadow = wlr_scene_drop_shadow_from_node(node);
		struct wlr_scene_buffer *scene_buffer
			= wlr_scene_drop_shadow_get_reference_buffer(drop_shadow);
		if (!scene_buffer || !scene_buffer->node.enabled) {
			return true;
		}
		return drop_shadow->blur_sigma == 0.0f
			|| drop_shadow->color[3] == 0.0f
			|| !linked_node_initialized(&drop_shadow->buffer_source);
	} else if (node->type == WLR_SCENE_NODE_OPTIMIZED_BLUR) {
		return false;
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);

		return buffer->buffer == NULL && buffer->texture == NULL;
	}

	return false;
}

struct render_list_constructor_data {
	struct wlr_box box;
	struct wl_array *render_list;
	bool calculate_visibility;
	bool highlight_transparent_region;
	bool fractional_scale;
};

static bool scene_buffer_is_black_opaque(struct wlr_scene_buffer *scene_buffer) {
	return scene_buffer->is_single_pixel_buffer &&
		scene_buffer->single_pixel_buffer_color[0] == 0 &&
		scene_buffer->single_pixel_buffer_color[1] == 0 &&
		scene_buffer->single_pixel_buffer_color[2] == 0 &&
		scene_buffer->single_pixel_buffer_color[3] == UINT32_MAX &&
		scene_buffer->opacity == 1.0 &&
		fx_corner_radii_is_empty(&scene_buffer->corners);
}

static bool scene_rect_is_black_opaque(struct wlr_scene_rect *scene_rect) {
	return scene_rect->color[0] == 0.f &&
		scene_rect->color[1] == 0.f &&
		scene_rect->color[2] == 0.f &&
		scene_rect->color[3] == 1.f &&
		fx_corner_radii_is_empty(&scene_rect->corners) &&
		fx_corner_radii_is_empty(&scene_rect->clipped_region.corners) &&
		wlr_box_empty(&scene_rect->clipped_region.area);
}

static bool construct_render_list_iterator(struct wlr_scene_node *node,
		int lx, int ly, void *_data) {
	struct render_list_constructor_data *data = _data;

	if (scene_node_invisible(node)) {
		return false;
	}

	// While rendering, the background should always be black. If we see a
	// black rect, we can ignore rendering everything under the rect, and
	// unless fractional scale is used even the rect itself (to avoid running
	// into issues regarding damage region expansion).
	if (node->type == WLR_SCENE_NODE_RECT && data->calculate_visibility &&
			(!data->fractional_scale || data->render_list->size == 0)) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);

		if (scene_rect_is_black_opaque(rect)) {
			return false;
		}
	}

	// Apply the same special-case to black opaque single-pixel buffers
	if (node->type == WLR_SCENE_NODE_BUFFER && data->calculate_visibility &&
			(!data->fractional_scale || data->render_list->size == 0)) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		if (scene_buffer_is_black_opaque(scene_buffer)) {
			return false;
		}
	}

	pixman_region32_t intersection;
	pixman_region32_init(&intersection);
	pixman_region32_intersect_rect(&intersection, &node->visible,
			data->box.x, data->box.y,
			data->box.width, data->box.height);
	if (pixman_region32_empty(&intersection)) {
		pixman_region32_fini(&intersection);
		return false;
	}

	pixman_region32_fini(&intersection);

	struct render_list_entry *entry = wl_array_add(data->render_list, sizeof(*entry));
	if (!entry) {
		return false;
	}

	*entry = (struct render_list_entry){
		.node = node,
		.x = lx,
		.y = ly,
		.highlight_transparent_region = data->highlight_transparent_region,
	};

	return false;
}

static void scene_buffer_send_dmabuf_feedback(const struct wlr_scene *scene,
		struct wlr_scene_buffer *scene_buffer,
		const struct wlr_linux_dmabuf_feedback_v1_init_options *options) {
	if (!scene->linux_dmabuf_v1) {
		return;
	}

	struct wlr_scene_surface *surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!surface) {
		return;
	}

	// compare to the previous options so that we don't send
	// duplicate feedback events.
	if (memcmp(options, &scene_buffer->prev_feedback_options, sizeof(*options)) == 0) {
		return;
	}

	scene_buffer->prev_feedback_options = *options;

	struct wlr_linux_dmabuf_feedback_v1 feedback = {0};
	if (!wlr_linux_dmabuf_feedback_v1_init_with_options(&feedback, options)) {
		return;
	}

	wlr_linux_dmabuf_v1_set_surface_feedback(scene->linux_dmabuf_v1,
		surface->surface, &feedback);

	wlr_linux_dmabuf_feedback_v1_finish(&feedback);
}

enum scene_direct_scanout_result {
	// This scene node is not a candidate for scanout
	SCANOUT_INELIGIBLE,

	// This scene node is a candidate for scanout, but is currently
	// incompatible due to e.g. buffer mismatch, and if possible we'd like to
	// resolve this incompatibility.
	SCANOUT_CANDIDATE,

	// Scanout is successful.
	SCANOUT_SUCCESS,
};

static enum scene_direct_scanout_result scene_entry_try_direct_scanout(
		struct render_list_entry *entry, struct wlr_output_state *state,
		const struct render_data *data) {
	struct wlr_scene_output *scene_output = data->output;
	struct wlr_scene_node *node = entry->node;

	if (!scene_output->scene->direct_scanout) {
		return SCANOUT_INELIGIBLE;
	}

	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return SCANOUT_INELIGIBLE;
	}

	if (state->committed & (WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_ENABLED |
			WLR_OUTPUT_STATE_RENDER_FORMAT)) {
		// Legacy DRM will explode if we try to modeset with a direct scanout buffer
		return SCANOUT_INELIGIBLE;
	}

	if (!wlr_output_is_direct_scanout_allowed(scene_output->output)) {
		return SCANOUT_INELIGIBLE;
	}

	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	if (buffer->buffer == NULL) {
		return SCANOUT_INELIGIBLE;
	}

	// The native size of the buffer after any transform is applied
	int default_width = buffer->buffer->width;
	int default_height = buffer->buffer->height;
	wlr_output_transform_coords(buffer->transform, &default_width, &default_height);
	struct wlr_fbox default_box = {
		.width = default_width,
		.height = default_height,
	};

	if (buffer->transform != data->transform) {
		return SCANOUT_INELIGIBLE;
	}

	// We want to ensure optimal buffer selection, but as direct-scanout can be enabled and disabled
	// on a frame-by-frame basis, we wait for a few frames to send the new format recommendations.
	// Maybe we should only send feedback in this case if tests fail.
	if (scene_output->dmabuf_feedback_debounce >= DMABUF_FEEDBACK_DEBOUNCE_FRAMES
			&& buffer->primary_output == scene_output) {
		struct wlr_linux_dmabuf_feedback_v1_init_options options = {
			.main_renderer = scene_output->output->renderer,
			.scanout_primary_output = scene_output->output,
		};

		scene_buffer_send_dmabuf_feedback(scene_output->scene, buffer, &options);
	}

	struct wlr_output_state pending;
	wlr_output_state_init(&pending);
	if (!wlr_output_state_copy(&pending, state)) {
		return SCANOUT_CANDIDATE;
	}

	if (!wlr_fbox_empty(&buffer->src_box) &&
			!wlr_fbox_equal(&buffer->src_box, &default_box)) {
		pending.buffer_src_box = buffer->src_box;
	}

	// Translate the position from scene coordinates to output coordinates
	pending.buffer_dst_box.x = entry->x - scene_output->x;
	pending.buffer_dst_box.y = entry->y - scene_output->y;

	scene_node_get_size(node, &pending.buffer_dst_box.width, &pending.buffer_dst_box.height);
	transform_output_box(&pending.buffer_dst_box, data);

	struct wlr_buffer *wlr_buffer = buffer->buffer;
	struct wlr_client_buffer *client_buffer = wlr_client_buffer_get(wlr_buffer);
	if (client_buffer != NULL && client_buffer->source != NULL && client_buffer->source->n_locks > 0) {
		wlr_buffer = client_buffer->source;
	}

	wlr_output_state_set_buffer(&pending, wlr_buffer);
	if (buffer->wait_timeline != NULL) {
		wlr_output_state_set_wait_timeline(&pending, buffer->wait_timeline, buffer->wait_point);
	}
	if (!wlr_output_test_state(scene_output->output, &pending)) {
		wlr_output_state_finish(&pending);
		return SCANOUT_CANDIDATE;
	}

	wlr_output_state_copy(state, &pending);
	wlr_output_state_finish(&pending);

	struct wlr_scene_output_sample_event sample_event = {
		.output = scene_output,
		.direct_scanout = true,
	};
	wl_signal_emit_mutable(&buffer->events.output_sample, &sample_event);
	return SCANOUT_SUCCESS;
}

bool wlr_scene_output_needs_frame(struct wlr_scene_output *scene_output) {
	return scene_output->output->needs_frame ||
		!pixman_region32_empty(&scene_output->pending_commit_damage) ||
		scene_output->gamma_lut_changed;
}

static void apply_blur_region(struct wlr_scene_node *node,
		struct wlr_scene_output *scene_output, pixman_region32_t *blur_region) {
	int x, y;
	wlr_scene_node_coords(node, &x, &y);

	struct wlr_box node_box = {
		.x = x - scene_output->x,
		.y = y - scene_output->y,
	};
	scene_node_get_size(node, &node_box.width, &node_box.height);

	struct wlr_output *output = scene_output->output;

	int output_width, output_height;
	wlr_output_transformed_resolution(output, &output_width, &output_height);

	// Transform the box back to regular un-transformed units
	scale_box(&node_box, output->scale);
	wlr_box_transform(&node_box, &node_box,
			wlr_output_transform_invert(output->transform),
			output_width, output_height);

	pixman_region32_union_rect(blur_region, blur_region,
			node_box.x, node_box.y,
			node_box.width, node_box.height);
}

static bool scene_output_has_blur(int list_len,
		struct render_list_entry *list_data, struct wlr_scene_output *scene_output,
		pixman_region32_t *blur_region) {
	if (scene_output->scene->blur_data.radius <= 0 ||
			scene_output->scene->blur_data.num_passes <= 0) {
		return false;
	}

	for (int i = list_len - 1; i >= 0; i--) {
		struct wlr_scene_node *node = list_data[i].node;
		switch (node->type) {
		case WLR_SCENE_NODE_BLUR:;
			apply_blur_region(node, scene_output, blur_region);
			break;
		case WLR_SCENE_NODE_OPTIMIZED_BLUR:;
			struct wlr_scene_optimized_blur *scene_blur = wlr_scene_optimized_blur_from_node(node);
			if (scene_blur->dirty) {
				apply_blur_region(node, scene_output, blur_region);
			}
			break;
		default:
			// TODO: Add support for other node types
			break;
		}
	}
	return !pixman_region32_empty(blur_region);
}

bool wlr_scene_output_commit(struct wlr_scene_output *scene_output,
		const struct wlr_scene_output_state_options *options) {
	if (!wlr_scene_output_needs_frame(scene_output)) {
		return true;
	}

	bool ok = false;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (!wlr_scene_output_build_state(scene_output, &state, options)) {
		goto out;
	}

	ok = wlr_output_commit_state(scene_output->output, &state);
	if (!ok) {
		goto out;
	}

out:
	wlr_output_state_finish(&state);
	return ok;
}

static void scene_output_state_attempt_gamma(struct wlr_scene_output *scene_output,
		struct wlr_output_state *state) {
	if (!scene_output->gamma_lut_changed) {
		return;
	}

	struct wlr_output_state gamma_pending = {0};
	if (!wlr_output_state_copy(&gamma_pending, state)) {
		return;
	}

	if (!wlr_gamma_control_v1_apply(scene_output->gamma_lut, &gamma_pending)) {
		wlr_output_state_finish(&gamma_pending);
		return;
	}

	scene_output->gamma_lut_changed = false;
	if (!wlr_output_test_state(scene_output->output, &gamma_pending)) {
		wlr_gamma_control_v1_send_failed_and_destroy(scene_output->gamma_lut);

		scene_output->gamma_lut = NULL;
		wlr_output_state_finish(&gamma_pending);
		return;
	}

	wlr_output_state_copy(state, &gamma_pending);
	wlr_output_state_finish(&gamma_pending);
}

bool wlr_scene_output_build_state(struct wlr_scene_output *scene_output,
		struct wlr_output_state *state, const struct wlr_scene_output_state_options *options) {
	struct wlr_scene_output_state_options default_options = {0};
	if (!options) {
		options = &default_options;
	}
	struct wlr_scene_timer *timer = options->timer;
	struct timespec start_time;
	if (timer) {
		clock_gettime(CLOCK_MONOTONIC, &start_time);
		wlr_scene_timer_finish(timer);
		*timer = (struct wlr_scene_timer){0};
	}

	if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && !state->enabled) {
		// if the state is being disabled, do nothing.
		return true;
	}

	struct wlr_output *output = scene_output->output;
	enum wlr_scene_debug_damage_option debug_damage =
		scene_output->scene->debug_damage_option;

	struct render_data render_data = {
		.transform = output->transform,
		.scale = output->scale,
		.logical = { .x = scene_output->x, .y = scene_output->y },
		.output = scene_output,
		.has_blur = false,
	};

	int resolution_width, resolution_height;
	output_pending_resolution(output, state,
		&resolution_width, &resolution_height);

	if (state->committed & WLR_OUTPUT_STATE_TRANSFORM) {
		if (render_data.transform != state->transform) {
			scene_output_damage_whole(scene_output);
		}

		render_data.transform = state->transform;
	}

	if (state->committed & WLR_OUTPUT_STATE_SCALE) {
		if (render_data.scale != state->scale) {
			scene_output_damage_whole(scene_output);
		}

		render_data.scale = state->scale;
	}

	render_data.trans_width = resolution_width;
	render_data.trans_height = resolution_height;
	wlr_output_transform_coords(render_data.transform,
		&render_data.trans_width, &render_data.trans_height);

	render_data.logical.width = render_data.trans_width / render_data.scale;
	render_data.logical.height = render_data.trans_height / render_data.scale;

	struct render_list_constructor_data list_con = {
		.box = render_data.logical,
		.render_list = &scene_output->render_list,
		.calculate_visibility = scene_output->scene->calculate_visibility,
		.highlight_transparent_region = scene_output->scene->highlight_transparent_region,
		.fractional_scale = floor(render_data.scale) != render_data.scale,
	};

	list_con.render_list->size = 0;
	scene_nodes_in_box(&scene_output->scene->tree.node, &list_con.box,
		construct_render_list_iterator, &list_con);
	array_realloc(list_con.render_list, list_con.render_list->size);

	struct render_list_entry *list_data = list_con.render_list->data;
	int list_len = list_con.render_list->size / sizeof(*list_data);

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_RERENDER) {
		scene_output_damage_whole(scene_output);
	}

	struct timespec now;
	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct wl_list *regions = &scene_output->damage_highlight_regions;
		clock_gettime(CLOCK_MONOTONIC, &now);

		// add the current frame's damage if there is damage
		if (!pixman_region32_empty(&scene_output->damage_ring.current)) {
			struct highlight_region *current_damage = calloc(1, sizeof(*current_damage));
			if (current_damage) {
				pixman_region32_init(&current_damage->region);
				pixman_region32_copy(&current_damage->region,
					&scene_output->damage_ring.current);
				current_damage->when = now;
				wl_list_insert(regions, &current_damage->link);
			}
		}

		pixman_region32_t acc_damage;
		pixman_region32_init(&acc_damage);
		struct highlight_region *damage, *tmp_damage;
		wl_list_for_each_safe(damage, tmp_damage, regions, link) {
			// remove overlaping damage regions
			pixman_region32_subtract(&damage->region, &damage->region, &acc_damage);
			pixman_region32_union(&acc_damage, &acc_damage, &damage->region);

			// if this damage is too old or has nothing in it, get rid of it
			struct timespec time_diff;
			timespec_sub(&time_diff, &now, &damage->when);
			if (timespec_to_msec(&time_diff) >= HIGHLIGHT_DAMAGE_FADEOUT_TIME ||
					pixman_region32_empty(&damage->region)) {
				highlight_region_destroy(damage);
			}
		}

		scene_output_damage(scene_output, &acc_damage);
		pixman_region32_fini(&acc_damage);
	}

	wlr_output_state_set_damage(state, &scene_output->pending_commit_damage);

	// We only want to try direct scanout if:
	// - There is only one entry in the render list
	// - There are no color transforms that need to be applied
	// - Damage highlight debugging is not enabled
	enum scene_direct_scanout_result scanout_result = SCANOUT_INELIGIBLE;
	if (options->color_transform == NULL && list_len == 1
			&& debug_damage != WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		scanout_result = scene_entry_try_direct_scanout(&list_data[0], state, &render_data);
	}

	if (scanout_result == SCANOUT_INELIGIBLE) {
		if (scene_output->dmabuf_feedback_debounce > 0) {
			// We cannot scan out, so count down towards sending composition dmabuf feedback
			scene_output->dmabuf_feedback_debounce--;
		}
	} else if (scene_output->dmabuf_feedback_debounce < DMABUF_FEEDBACK_DEBOUNCE_FRAMES) {
		// We either want to scan out or successfully scanned out, so count up towards sending
		// scanout dmabuf feedback
		scene_output->dmabuf_feedback_debounce++;
	}

	bool scanout = scanout_result == SCANOUT_SUCCESS;
	if (scene_output->prev_scanout != scanout) {
		scene_output->prev_scanout = scanout;
		wlr_log(WLR_DEBUG, "Direct scan-out %s",
			scanout ? "enabled" : "disabled");
	}

	if (scanout) {
		scene_output_state_attempt_gamma(scene_output, state);

		if (timer) {
			struct timespec end_time, duration;
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			timespec_sub(&duration, &end_time, &start_time);
			timer->pre_render_duration = timespec_to_nsec(&duration);
		}
		return true;
	}

	struct wlr_swapchain *swapchain = options->swapchain;
	if (!swapchain) {
		if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) {
			return false;
		}

		swapchain = output->swapchain;
	}

	struct wlr_buffer *buffer = wlr_swapchain_acquire(swapchain);
	if (buffer == NULL) {
		return false;
	}

	assert(buffer->width == resolution_width && buffer->height == resolution_height);

	if (timer) {
		timer->render_timer = wlr_render_timer_create(output->renderer);

		struct timespec end_time, duration;
		clock_gettime(CLOCK_MONOTONIC, &end_time);
		timespec_sub(&duration, &end_time, &start_time);
		timer->pre_render_duration = timespec_to_nsec(&duration);
	}

	scene_output->in_point++;
	struct fx_gles_render_pass *render_pass =
		fx_renderer_begin_buffer_pass(output->renderer, buffer, output,
				&(struct fx_buffer_pass_options) {
					.base = &(struct wlr_buffer_pass_options){
						.timer = timer ? timer->render_timer : NULL,
						.color_transform = options->color_transform,
						.signal_timeline = scene_output->in_timeline,
						.signal_point = scene_output->in_point,
					},
					.swapchain = swapchain,
				}
			);
	if (render_pass == NULL) {
		wlr_buffer_unlock(buffer);
		return false;
	}
	struct fx_effect_framebuffers *effect_fbos = render_pass->fx_effect_framebuffers;

	render_data.render_pass = render_pass;
	pixman_region32_init(&render_data.damage);
	wlr_damage_ring_rotate_buffer(&scene_output->damage_ring, buffer,
		&render_data.damage);

	// Blur artifact prevention
	pixman_region32_t blur_region;
	pixman_region32_init(&blur_region);
	render_data.has_blur =
		scene_output_has_blur(list_len, list_data, scene_output, &blur_region);
	bool whole_output_blur_damaged = false;
	// Expand the damage to compensate for blur
	if (render_data.has_blur) {
		int output_width = output->width;
		int output_height = output->height;
		struct blur_data *blur_data = &scene_output->scene->blur_data;
		pixman_region32_t *damage = &render_data.damage;

		// ensure that the damage isn't expanding past the output's size
		int32_t damage_width = damage->extents.x2 - damage->extents.x1;
		int32_t damage_height = damage->extents.y2 - damage->extents.y1;
		if (damage_width > output_width || damage_height > output_height) {
			pixman_region32_intersect_rect(damage, damage,
					0, 0, output_width, output_height);
			// No need to compensate for blur artifacts when the damage spans
			// the whole output
			whole_output_blur_damaged = true;
		} else {
			// copy the surrounding content where the blur would display artifacts
			// and draw it above the artifacts
			pixman_region32_t extended_damage;
			pixman_region32_init(&extended_damage);
			pixman_region32_intersect(&extended_damage, damage, &blur_region);
			// Expand the region to compensate for blur artifacts
			wlr_region_expand(&extended_damage, &extended_damage, blur_data_calc_size(blur_data));
			// Limit to the monitors viewport
			pixman_region32_intersect_rect(&extended_damage, &extended_damage,
					0, 0, output_width, output_height);

			// capture the padding pixels around the blur where artifacts will be drawn
			pixman_region32_subtract(&effect_fbos->blur_padding_region,
					&extended_damage, damage);
			// Combine into the surface damage (we need to redraw the padding area as well)
			pixman_region32_union(damage, damage, &extended_damage);
			pixman_region32_fini(&extended_damage);

			// Capture the padding pixels before blur for later use
			fx_renderer_read_to_buffer(render_pass, &effect_fbos->blur_padding_region,
					effect_fbos->blur_saved_pixels_buffer,
					render_pass->buffer);
		}
	}
	pixman_region32_fini(&blur_region);

	pixman_region32_t background;
	pixman_region32_init(&background);
	pixman_region32_copy(&background, &render_data.damage);

	// Cull areas of the background that are occluded by opaque regions of
	// scene nodes above. Those scene nodes will just render atop having us
	// never see the background.
	if (scene_output->scene->calculate_visibility) {
		for (int i = list_len - 1; i >= 0; i--) {
			struct render_list_entry *entry = &list_data[i];

			// We must only cull opaque regions that are visible by the node.
			// The node's visibility will have the knowledge of a black rect
			// that may have been omitted from the render list via the black
			// rect optimization. In order to ensure we don't cull background
			// rendering in that black rect region, consider the node's visibility.
			pixman_region32_t opaque;
			pixman_region32_init(&opaque);
			scene_node_opaque_region(entry->node, entry->x, entry->y, &opaque);
			pixman_region32_intersect(&opaque, &opaque, &entry->node->visible);

			pixman_region32_translate(&opaque, -scene_output->x, -scene_output->y);
			logical_to_buffer_coords(&opaque, &render_data, false);
			pixman_region32_subtract(&background, &background, &opaque);
			pixman_region32_fini(&opaque);
		}

		if (floor(render_data.scale) != render_data.scale) {
			wlr_region_expand(&background, &background, 1);

			// reintersect with the damage because we never want to render
			// outside of the damage region
			pixman_region32_intersect(&background, &background, &render_data.damage);
		}
	}

	wlr_render_pass_add_rect(&render_pass->base, &(struct wlr_render_rect_options){
		.box = { .width = buffer->width, .height = buffer->height },
		.color = { .r = 0, .g = 0, .b = 0, .a = 1 },
		.clip = &background,
	});
	pixman_region32_fini(&background);

	for (int i = list_len - 1; i >= 0; i--) {
		struct render_list_entry *entry = &list_data[i];
		scene_entry_render(entry, &render_data);

		if (entry->node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(entry->node);

			// Direct scanout counts up to DMABUF_FEEDBACK_DEBOUNCE_FRAMES before sending new dmabuf
			// feedback, and on composition we wait until it hits zero again. If we knew that an
			// entry could never be a scanout candidate, we could send feedback to it
			// unconditionally without debounce, but for now it is all or nothing
			if (scene_output->dmabuf_feedback_debounce == 0 && buffer->primary_output == scene_output) {
				struct wlr_linux_dmabuf_feedback_v1_init_options options = {
					.main_renderer = output->renderer,
					.scanout_primary_output = NULL,
				};

				scene_buffer_send_dmabuf_feedback(scene_output->scene, buffer, &options);
			}
		}
	}

	if (render_data.has_blur && !whole_output_blur_damaged) {
		// Render the saved pixels over the blur artifacts
		fx_renderer_read_to_buffer(render_pass, &effect_fbos->blur_padding_region,
				render_pass->buffer, effect_fbos->blur_saved_pixels_buffer);
	}

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct highlight_region *damage;
		wl_list_for_each(damage, &scene_output->damage_highlight_regions, link) {
			struct timespec time_diff;
			timespec_sub(&time_diff, &now, &damage->when);
			int64_t time_diff_ms = timespec_to_msec(&time_diff);
			float alpha = 1.0 - (double)time_diff_ms / HIGHLIGHT_DAMAGE_FADEOUT_TIME;

			wlr_render_pass_add_rect(&render_pass->base, &(struct wlr_render_rect_options){
				.box = { .width = buffer->width, .height = buffer->height },
				.color = { .r = alpha * 0.5, .g = 0, .b = 0, .a = alpha * 0.5 },
				.clip = &damage->region,
			});
		}
	}

	wlr_output_add_software_cursors_to_render_pass(output, &render_pass->base, &render_data.damage);
	pixman_region32_fini(&render_data.damage);

	if (!wlr_render_pass_submit(&render_pass->base)) {
		TRACY_MARK_FRAME;
		wlr_buffer_unlock(buffer);

		// if we failed to render the buffer, it will have undefined contents
		// Trash the damage ring
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
		return false;
	}
	TRACY_MARK_FRAME;

	wlr_output_state_set_buffer(state, buffer);
	wlr_buffer_unlock(buffer);

	if (scene_output->in_timeline != NULL) {
		wlr_output_state_set_wait_timeline(state, scene_output->in_timeline,
			scene_output->in_point);
	}

	scene_output_state_attempt_gamma(scene_output, state);

	return true;
}

int64_t wlr_scene_timer_get_duration_ns(struct wlr_scene_timer *timer) {
	int64_t pre_render = timer->pre_render_duration;
	if (!timer->render_timer) {
		return pre_render;
	}
	int64_t render = wlr_render_timer_get_duration_ns(timer->render_timer);
	return render != -1 ? pre_render + render : -1;
}

void wlr_scene_timer_finish(struct wlr_scene_timer *timer) {
	if (timer->render_timer) {
		wlr_render_timer_destroy(timer->render_timer);
	}
}

static void scene_node_send_frame_done(struct wlr_scene_node *node,
		struct wlr_scene_output *scene_output, struct timespec *now) {
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer =
			wlr_scene_buffer_from_node(node);

		if (scene_buffer->primary_output == scene_output) {
			wlr_scene_buffer_send_frame_done(scene_buffer, now);
		}
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_send_frame_done(child, scene_output, now);
		}
	}
}

void wlr_scene_output_send_frame_done(struct wlr_scene_output *scene_output,
		struct timespec *now) {
	scene_node_send_frame_done(&scene_output->scene->tree.node,
		scene_output, now);
}

static void scene_output_for_each_scene_buffer(const struct wlr_box *output_box,
		struct wlr_scene_node *node, int lx, int ly,
		wlr_scene_buffer_iterator_func_t user_iterator, void *user_data) {
	if (!node->enabled) {
		return;
	}

	lx += node->x;
	ly += node->y;

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_box node_box = { .x = lx, .y = ly };
		scene_node_get_size(node, &node_box.width, &node_box.height);

		struct wlr_box intersection;
		if (wlr_box_intersection(&intersection, output_box, &node_box)) {
			struct wlr_scene_buffer *scene_buffer =
				wlr_scene_buffer_from_node(node);
			user_iterator(scene_buffer, lx, ly, user_data);
		}
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_output_for_each_scene_buffer(output_box, child, lx, ly,
				user_iterator, user_data);
		}
	}
}

void wlr_scene_output_for_each_buffer(struct wlr_scene_output *scene_output,
		wlr_scene_buffer_iterator_func_t iterator, void *user_data) {
	struct wlr_box box = { .x = scene_output->x, .y = scene_output->y };
	wlr_output_effective_resolution(scene_output->output,
		&box.width, &box.height);
	scene_output_for_each_scene_buffer(&box, &scene_output->scene->tree.node, 0, 0,
		iterator, user_data);
}
