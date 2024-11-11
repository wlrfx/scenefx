#include <assert.h>
#include <pixman.h>
#include <stdlib.h>
#include <scenefx/types/wlr_scene.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include <wlr/render/swapchain.h>

#include "scenefx/render/pass.h"
#include "types/wlr_buffer.h"
#include "types/wlr_output.h"
#include "types/wlr_scene.h"
#include "util/array.h"
#include "util/env.h"
#include "util/time.h"

#define HIGHLIGHT_DAMAGE_FADEOUT_TIME 250

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
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);

		if (scene_tree == &scene->tree) {
			assert(!node->parent);
			struct wlr_scene_output *scene_output, *scene_output_tmp;
			wl_list_for_each_safe(scene_output, scene_output_tmp, &scene->outputs, link) {
				wlr_scene_output_destroy(scene_output);
			}

			wl_list_remove(&scene->linux_dmabuf_v1_destroy.link);
		} else {
			assert(node->parent);
		}

		struct wlr_scene_node *child, *child_tmp;
		wl_list_for_each_safe(child, child_tmp,
				&scene_tree->children, link) {
			wlr_scene_node_destroy(child);
		}
	}

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
	case WLR_SCENE_NODE_RECT:
	case WLR_SCENE_NODE_SHADOW:
	case WLR_SCENE_NODE_BUFFER:;
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

static void scene_node_opaque_region(struct wlr_scene_node *node, int x, int y,
		pixman_region32_t *opaque) {
	int width, height;
	scene_node_get_size(node, &width, &height);

	if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);
		if (scene_rect->corner_radius > 0) {
			// TODO: this is incorrect
			return;
		}
		if (scene_rect->color[3] != 1) {
			return;
		}
	} else if (node->type == WLR_SCENE_NODE_SHADOW) {
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

		if (scene_buffer->corner_radius > 0) {
			// TODO: this is incorrect
			return;
		}

		if (!scene_buffer->buffer_is_opaque) {
			pixman_region32_copy(opaque, &scene_buffer->opaque_region);
			pixman_region32_intersect_rect(opaque, opaque, 0, 0, width, height);
			pixman_region32_translate(opaque, x, y);
			return;
		}
	}

	pixman_region32_fini(opaque);
	pixman_region32_init_rect(opaque, x, y, width, height);
}

struct scene_update_data {
	pixman_region32_t *visible;
	pixman_region32_t *update_region;
	struct wl_list *outputs;
	bool calculate_visibility;
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

static void scale_output_damage(pixman_region32_t *damage, float scale) {
	wlr_region_scale(damage, damage, scale);

	if (floor(scale) != scale) {
		wlr_region_expand(damage, damage, 1);
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
};

static void transform_output_damage(pixman_region32_t *damage, const struct render_data *data) {
	enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
	wlr_region_transform(damage, damage, transform, data->trans_width, data->trans_height);
}

static void scene_output_damage(struct wlr_scene_output *scene_output,
		const pixman_region32_t *region) {
	if (wlr_damage_ring_add(&scene_output->damage_ring, region)) {
		wlr_output_schedule_frame(scene_output->output);

		struct wlr_output *output = scene_output->output;
		enum wl_output_transform transform =
			wlr_output_transform_invert(scene_output->output->transform);

		int width = output->width;
		int height = output->height;
		if (transform & WL_OUTPUT_TRANSFORM_90) {
			width = output->height;
			height = output->width;
		}

		pixman_region32_t frame_damage;
		pixman_region32_init(&frame_damage);
		wlr_region_transform(&frame_damage, region, transform, width, height);

		pixman_region32_union(&scene_output->pending_commit_damage,
				&scene_output->pending_commit_damage, &frame_damage);
		pixman_region32_intersect_rect(&scene_output->pending_commit_damage,
				&scene_output->pending_commit_damage, 0, 0, output->width, output->height);
		pixman_region32_fini(&frame_damage);
	}
}

static void scene_output_damage_whole(struct wlr_scene_output *scene_output) {
 struct wlr_damage_ring *ring = &scene_output->damage_ring;

 pixman_region32_t damage;
 pixman_region32_init_rect(&damage, 0, 0, ring->width, ring->height);
 scene_output_damage(scene_output, &damage);
 pixman_region32_fini(&damage);
}

static void transform_output_box(struct wlr_box *box, const struct render_data *data) {
	enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
	wlr_box_transform(box, box, transform, data->trans_width, data->trans_height);
}

static void scene_damage_outputs(struct wlr_scene *scene, pixman_region32_t *damage) {
	if (!pixman_region32_not_empty(damage)) {
		return;
	}

	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		pixman_region32_t output_damage;
		pixman_region32_init(&output_damage);
		pixman_region32_copy(&output_damage, damage);
		pixman_region32_translate(&output_damage,
			-scene_output->x, -scene_output->y);
		scale_output_damage(&output_damage, scene_output->output->scale);
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

		if (pixman_region32_not_empty(&intersection)) {
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

static bool scene_node_update_iterator(struct wlr_scene_node *node,
		int lx, int ly, void *_data) {
	struct scene_update_data *data = _data;

	struct wlr_box box = { .x = lx, .y = ly };
	scene_node_get_size(node, &box.width, &box.height);

	pixman_region32_subtract(&node->visible, &node->visible, data->update_region);
	pixman_region32_union(&node->visible, &node->visible, data->visible);
	pixman_region32_intersect_rect(&node->visible, &node->visible,
		lx, ly, box.width, box.height);

	if (data->calculate_visibility) {
		pixman_region32_t opaque;
		pixman_region32_init(&opaque);
		scene_node_opaque_region(node, lx, ly, &opaque);
		pixman_region32_subtract(data->visible, data->visible, &opaque);
		pixman_region32_fini(&opaque);
	}

	update_node_update_outputs(node, data->outputs, NULL, NULL);

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

	struct scene_update_data data = {
		.visible = &visible,
		.update_region = update_region,
		.outputs = &scene->outputs,
		.calculate_visibility = scene->calculate_visibility,
	};

	struct pixman_box32 *region_box = pixman_region32_extents(update_region);
	struct wlr_box box = {
		.x = region_box->x1,
		.y = region_box->y1,
		.width = region_box->x2 - region_box->x1,
		.height = region_box->y2 - region_box->y1,
	};

	// update node visibility and output enter/leave events
	scene_nodes_in_box(&scene->tree.node, &box, scene_node_update_iterator, &data);

	pixman_region32_fini(&visible);
}

static void scene_node_update(struct wlr_scene_node *node,
		pixman_region32_t *damage) {
	struct wlr_scene *scene = scene_node_get_root(node);

	int x, y;
	if (!wlr_scene_node_coords(node, &x, &y)) {
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
	struct wlr_scene_rect *scene_rect = calloc(1, sizeof(*scene_rect));
	if (scene_rect == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_rect->node, WLR_SCENE_NODE_RECT, parent);

	scene_rect->width = width;
	scene_rect->height = height;
	memcpy(scene_rect->color, color, sizeof(scene_rect->color));
	scene_rect->corner_radius = 0;

	scene_node_update(&scene_rect->node, NULL);

	return scene_rect;
}

void wlr_scene_rect_set_size(struct wlr_scene_rect *rect, int width, int height) {
	if (rect->width == width && rect->height == height) {
		return;
	}

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
	scene_buffer->buffer_is_opaque = buffer_is_opaque(buffer);

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

void wlr_scene_rect_set_corner_radius(struct wlr_scene_rect *rect, int corner_radius) {
	if (rect->corner_radius == corner_radius) {
		return;
	}

	rect->corner_radius = corner_radius;
	scene_node_update(&rect->node, NULL);
}

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
	scene_buffer->corner_radius = 0;
	scene_buffer->corners = CORNER_LOCATION_NONE;

	scene_buffer_set_buffer(scene_buffer, buffer);
	scene_node_update(&scene_buffer->node, NULL);

	return scene_buffer;
}

void wlr_scene_buffer_set_buffer_with_damage(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer, const pixman_region32_t *damage) {
	// specifying a region for a NULL buffer doesn't make sense. We need to know
	// about the buffer to scale the buffer local coordinates down to scene
	// coordinates.
	assert(buffer || !damage);

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

	scene_buffer_set_buffer(scene_buffer, buffer);
	scene_buffer_set_texture(scene_buffer, NULL);

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
		scale_output_damage(&cull_region, output_scale);
		pixman_region32_translate(&cull_region, -lx * output_scale, -ly * output_scale);
		pixman_region32_intersect(&output_damage, &output_damage, &cull_region);
		pixman_region32_fini(&cull_region);

		pixman_region32_translate(&output_damage,
			(int)round((lx - scene_output->x) * output_scale),
			(int)round((ly - scene_output->y) * output_scale));
		scene_output_damage(scene_output, &output_damage);
		pixman_region32_fini(&output_damage);
	}

	pixman_region32_fini(&trans_damage);
	pixman_region32_fini(&fallback_damage);
}

void wlr_scene_buffer_set_buffer(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer)  {
	wlr_scene_buffer_set_buffer_with_damage(scene_buffer, buffer, NULL);
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
	if (pixman_region32_not_empty(&scene_buffer->node.visible)) {
		wl_signal_emit_mutable(&scene_buffer->events.frame_done, now);
	}
}

void wlr_scene_buffer_set_opacity(struct wlr_scene_buffer *scene_buffer,
		float opacity) {
	if (scene_buffer->opacity == opacity) {
		return;
	}

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

void wlr_scene_buffer_set_corner_radius(struct wlr_scene_buffer *scene_buffer,
		int radii, enum corner_location corners) {
	if (scene_buffer->corner_radius == radii
			&& scene_buffer->corners == corners) {
		return;
	}

	scene_buffer->corner_radius = radii;
	scene_buffer->corners = corners;
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
	}
}

static void scene_node_get_corner_radius(struct wlr_scene_node *node, int *corner_radius) {
	*corner_radius = 0;

	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		return;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);
		*corner_radius = scene_rect->corner_radius;
		return;
	case WLR_SCENE_NODE_SHADOW:;
		struct wlr_scene_shadow *scene_shadow = wlr_scene_shadow_from_node(node);
		*corner_radius = scene_shadow->corner_radius;
		break;
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		*corner_radius = scene_buffer->corner_radius;
		break;
	}
}

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

static void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
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
	} else if (node->type == WLR_SCENE_NODE_SHADOW) {
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

static void get_tree_geometry(struct wlr_scene_node *node,
		pixman_region32_t *region, int *max_corner_radius) {
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_BUFFER || node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_box child_box = {0};
		int radius = 0;
		wlr_scene_node_coords(node, &child_box.x, &child_box.y);
		scene_node_get_size(node, &child_box.width, &child_box.height);
		scene_node_get_corner_radius(node, &radius);

		pixman_region32_union_rect(region, region,
				child_box.x, child_box.y, child_box.width, child_box.height);
		if (radius > *max_corner_radius) {
			*max_corner_radius = radius;
		}
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *node;
		wl_list_for_each(node, &tree->children, link) {
			// Could possibly be a tree, so look through it as well
			get_tree_geometry(node, region, max_corner_radius);
		}
	}
}

static void scene_get_next_sibling_geometry(struct wlr_scene_node *node,
		struct wlr_box *box, int *corner_radius) {
	assert(box && corner_radius);

	*box = (struct wlr_box) {0};
	*corner_radius = 0;

	// Don't check if it's the only child in the tree
	if (node->link.next == node->link.prev) {
		return;
	}

	struct wlr_scene_node *sibling_node =
		wl_container_of(node->link.next, sibling_node, link);

	pixman_region32_t region;
	pixman_region32_init(&region);

	get_tree_geometry(sibling_node, &region, corner_radius);

	if (pixman_region32_not_empty(&region)) {
		struct pixman_box32 *region_box = pixman_region32_extents(&region);
		*box = (struct wlr_box){
			.x = region_box->x1,
			.y = region_box->y1,
			.width = region_box->x2 - region_box->x1,
			.height = region_box->y2 - region_box->y1,
		};
	}

	pixman_region32_fini(&region);
}

struct render_list_entry {
	struct wlr_scene_node *node;
	bool sent_dmabuf_feedback;
	bool highlight_transparent_region;
	int x, y;
};

static void scene_entry_render(struct render_list_entry *entry, const struct render_data *data) {
	struct wlr_scene_node *node = entry->node;

	pixman_region32_t render_region;
	pixman_region32_init(&render_region);
	pixman_region32_copy(&render_region, &node->visible);
	pixman_region32_translate(&render_region, -data->logical.x, -data->logical.y);
	scale_output_damage(&render_region, data->scale);
	pixman_region32_intersect(&render_region, &render_region, &data->damage);
	if (!pixman_region32_not_empty(&render_region)) {
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
	scale_box(&dst_box, data->scale);

	pixman_region32_t opaque;
	pixman_region32_init(&opaque);
	scene_node_opaque_region(node, x, y, &opaque);
	scale_output_damage(&opaque, data->scale);
	pixman_region32_subtract(&opaque, &render_region, &opaque);

	transform_output_box(&dst_box, data);
	transform_output_damage(&render_region, data);

	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		assert(false);
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);

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
		};

		if (scene_rect->corner_radius) {
			struct wlr_box window_box;
			int window_corner_radius;
			scene_get_next_sibling_geometry(node, &window_box, &window_corner_radius);

			window_corner_radius *= data->scale;
			scale_box(&window_box, data->scale);
			transform_output_box(&window_box, data);

			struct fx_render_rounded_rect_options rounded_rect_options = {
				.base = rect_options.base,
				.corner_radius = scene_rect->corner_radius,
				.corner_location = CORNER_LOCATION_ALL,
				.window_box = window_box,
				.window_corner_radius = window_corner_radius,
			};
			fx_render_pass_add_rounded_rect(data->render_pass, &rounded_rect_options);
		} else {
			fx_render_pass_add_rect(data->render_pass, &rect_options);
		}
		break;
	case WLR_SCENE_NODE_SHADOW:;
		struct wlr_scene_shadow *scene_shadow = wlr_scene_shadow_from_node(node);

		// Look through the first direct sibling node and get the size of the
		// node. If the sibling is a tree, get the total size of the whole tree.
		// Cannot grab the first buffer in the tree due to it potentially being
		// a CSD surface and not the actual toplevel surface.
		struct wlr_box window_box;
		int window_corner_radius;
		scene_get_next_sibling_geometry(node, &window_box, &window_corner_radius);

		window_corner_radius *= data->scale;
		scale_box(&window_box, data->scale);
		transform_output_box(&window_box, data);

		struct fx_render_box_shadow_options shadow_options = {
			.box = dst_box,
			// TODO: Use dst_box if larger?
			.window_box = window_box,
			.window_corner_radius = window_corner_radius,
			.blur_sigma = scene_shadow->blur_sigma,
			.corner_radius = scene_shadow->corner_radius,
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
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		struct wlr_texture *texture = scene_buffer_get_texture(scene_buffer,
			data->output->output->renderer);
		if (texture == NULL) {
			scene_output_damage(data->output, &render_region);
			break;
		}

		enum wl_output_transform transform =
			wlr_output_transform_invert(scene_buffer->transform);
		transform = wlr_output_transform_compose(transform, data->transform);

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
					pixman_region32_not_empty(&opaque) ?
					WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
			},
			.clip_box = &dst_box,
			.corners = scene_buffer->corners,
			.corner_radius = scene_buffer->corner_radius * data->scale,
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
	int ring_width, ring_height;
	wlr_output_transformed_resolution(scene_output->output, &ring_width, &ring_height);
	wlr_damage_ring_set_bounds(&scene_output->damage_ring, ring_width, ring_height);

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
}

static void scene_output_handle_damage(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_damage);
	struct wlr_output_event_damage *event = data;
	scene_output_damage(scene_output, event->damage);
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

static bool construct_render_list_iterator(struct wlr_scene_node *node,
		int lx, int ly, void *_data) {
	struct render_list_constructor_data *data = _data;

	if (scene_node_invisible(node)) {
		return false;
	}

	if (node->type == WLR_SCENE_NODE_RECT && data->calculate_visibility &&
			(!data->fractional_scale || data->render_list->size == 0)) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
		float *black = (float[4]){ 0.f, 0.f, 0.f, 1.f };

		if (memcmp(rect->color, black, sizeof(float) * 4) == 0) {
			return false;
		}
	}

	pixman_region32_t intersection;
	pixman_region32_init(&intersection);
	pixman_region32_intersect_rect(&intersection, &node->visible,
			data->box.x, data->box.y,
			data->box.width, data->box.height);
	if (!pixman_region32_not_empty(&intersection)) {
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

static bool scene_entry_try_direct_scanout(struct render_list_entry *entry,
		struct wlr_output_state *state, const struct render_data *data) {
	struct wlr_scene_output *scene_output = data->output;
	struct wlr_scene_node *node = entry->node;

	if (!scene_output->scene->direct_scanout) {
		return false;
	}

	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return false;
	}

	if (state->committed & (WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_ENABLED |
			WLR_OUTPUT_STATE_RENDER_FORMAT)) {
		// Legacy DRM will explode if we try to modeset with a direct scanout buffer
		return false;
	}

	if (!wlr_output_is_direct_scanout_allowed(scene_output->output)) {
		return false;
	}

	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);

	if (buffer->buffer == NULL) {
		return false;
	}

	int default_width = buffer->buffer->width;
	int default_height = buffer->buffer->height;
	wlr_output_transform_coords(buffer->transform, &default_width, &default_height);
	struct wlr_fbox default_box = {
			.width = default_width,
			.height = default_height,
	};

	if (!wlr_fbox_empty(&buffer->src_box) &&
			!wlr_fbox_equal(&buffer->src_box, &default_box)) {
		return false;
	}

	if (buffer->transform != data->transform) {
		return false;
	}

	struct wlr_box node_box = { .x = entry->x, .y = entry->y };
	scene_node_get_size(node, &node_box.width, &node_box.height);

	if (!wlr_box_equal(&data->logical, &node_box)) {
		return false;
	}

	if (buffer->primary_output == scene_output) {
		struct wlr_linux_dmabuf_feedback_v1_init_options options = {
			.main_renderer = scene_output->output->renderer,
			.scanout_primary_output = scene_output->output,
		};

		scene_buffer_send_dmabuf_feedback(scene_output->scene, buffer, &options);
		entry->sent_dmabuf_feedback = true;
	}

	struct wlr_output_state pending;
	wlr_output_state_init(&pending);
	if (!wlr_output_state_copy(&pending, state)) {
		return false;
	}

	wlr_output_state_set_buffer(&pending, buffer->buffer);

	if (!wlr_output_test_state(scene_output->output, &pending)) {
		wlr_output_state_finish(&pending);
		return false;
	}

	wlr_output_state_copy(state, &pending);
	wlr_output_state_finish(&pending);

	struct wlr_scene_output_sample_event sample_event = {
		.output = scene_output,
		.direct_scanout = true,
	};
	wl_signal_emit_mutable(&buffer->events.output_sample, &sample_event);
	return true;
}

bool wlr_scene_output_commit(struct wlr_scene_output *scene_output,
		const struct wlr_scene_output_state_options *options) {
	if (!scene_output->output->needs_frame && !pixman_region32_not_empty(
			&scene_output->pending_commit_damage)) {
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

	wlr_damage_ring_set_bounds(&scene_output->damage_ring,
			render_data.trans_width, render_data.trans_height);

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_RERENDER) {
		scene_output_damage_whole(scene_output);
	}

	struct timespec now;
	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct wl_list *regions = &scene_output->damage_highlight_regions;
		clock_gettime(CLOCK_MONOTONIC, &now);

		// add the current frame's damage if there is damage
		if (pixman_region32_not_empty(&scene_output->damage_ring.current)) {
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
					!pixman_region32_not_empty(&damage->region)) {
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
	bool scanout = options->color_transform == NULL &&
		list_len == 1 && debug_damage != WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT &&
		scene_entry_try_direct_scanout(&list_data[0], state, &render_data);

	if (scene_output->prev_scanout != scanout) {
		scene_output->prev_scanout = scanout;
		wlr_log(WLR_DEBUG, "Direct scan-out %s",
			scanout ? "enabled" : "disabled");
	}

	if (scanout) {
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

	struct wlr_buffer *buffer = wlr_swapchain_acquire(swapchain, NULL);
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

	struct fx_gles_render_pass *render_pass =
		fx_renderer_begin_buffer_pass(output->renderer, buffer, output,
				&(struct wlr_buffer_pass_options) {
					.timer = timer ? timer->render_timer : NULL,
					.color_transform = options->color_transform,
				}
			);
	if (render_pass == NULL) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	render_data.render_pass = render_pass;
	pixman_region32_init(&render_data.damage);
	wlr_damage_ring_rotate_buffer(&scene_output->damage_ring, buffer,
		&render_data.damage);

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
			wlr_region_scale(&opaque, &opaque, render_data.scale);
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

	transform_output_damage(&background, &render_data);
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

			if (buffer->primary_output == scene_output && !entry->sent_dmabuf_feedback) {
				struct wlr_linux_dmabuf_feedback_v1_init_options options = {
					.main_renderer = output->renderer,
					.scanout_primary_output = NULL,
				};

				scene_buffer_send_dmabuf_feedback(scene_output->scene, buffer, &options);
			}
		}
	}

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct highlight_region *damage;
		wl_list_for_each(damage, &scene_output->damage_highlight_regions, link) {
			struct timespec time_diff;
			timespec_sub(&time_diff, &now, &damage->when);
			int64_t time_diff_ms = timespec_to_msec(&time_diff);
			float alpha = 1.0 - (double)time_diff_ms / HIGHLIGHT_DAMAGE_FADEOUT_TIME;

			pixman_region32_t clip;
			pixman_region32_init(&clip);
			pixman_region32_copy(&clip, &damage->region);
			transform_output_damage(&clip, &render_data);

			wlr_render_pass_add_rect(&render_pass->base, &(struct wlr_render_rect_options){
				.box = { .width = buffer->width, .height = buffer->height },
				.color = { .r = alpha * 0.5, .g = 0, .b = 0, .a = alpha * 0.5 },
				.clip = &clip,
			});

			pixman_region32_fini(&clip);
		}
	}

	wlr_output_add_software_cursors_to_render_pass(output, &render_pass->base, &render_data.damage);

	pixman_region32_fini(&render_data.damage);

	if (!wlr_render_pass_submit(&render_pass->base)) {
		wlr_buffer_unlock(buffer);

		// if we failed to render the buffer, it will have undefined contents
		// Trash the damage ring
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
		return false;
	}

	wlr_output_state_set_buffer(state, buffer);
	wlr_buffer_unlock(buffer);

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
