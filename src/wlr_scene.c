#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "types/wlr_buffer.h"
#include "types/wlr_scene.h"
#include "util/array.h"
#include "util/env.h"
#include "util/time.h"

#define HIGHLIGHT_DAMAGE_FADEOUT_TIME 250

static struct wlr_scene_tree *scene_tree_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_TREE);
	struct wlr_scene_tree *tree = wl_container_of(node, tree, node);
	return tree;
}

static struct wlr_scene_rect *scene_rect_from_node(
		struct wlr_scene_node *node) {
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

struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node) {
	struct wlr_scene_tree *tree;
	if (node->type == WLR_SCENE_NODE_TREE) {
		tree = scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree->node.parent != NULL) {
		tree = tree->node.parent;
	}
	return (struct wlr_scene *)tree;
}

static void scene_node_init(struct wlr_scene_node *node,
		enum wlr_scene_node_type type, struct wlr_scene_tree *parent) {
	memset(node, 0, sizeof(*node));
	node->type = type;
	node->parent = parent;
	node->enabled = true;

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

		wlr_texture_destroy(scene_buffer->texture);
		wlr_buffer_unlock(scene_buffer->buffer);
		pixman_region32_fini(&scene_buffer->opaque_region);
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);

		if (scene_tree == &scene->tree) {
			assert(!node->parent);
			struct wlr_scene_output *scene_output, *scene_output_tmp;
			wl_list_for_each_safe(scene_output, scene_output_tmp, &scene->outputs, link) {
				wlr_scene_output_destroy(scene_output);
			}

			wl_list_remove(&scene->presentation_destroy.link);
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
	memset(tree, 0, sizeof(*tree));
	scene_node_init(&tree->node, WLR_SCENE_NODE_TREE, parent);
	wl_list_init(&tree->children);
}

struct wlr_scene *wlr_scene_create(void) {
	struct wlr_scene *scene = calloc(1, sizeof(struct wlr_scene));
	if (scene == NULL) {
		return NULL;
	}

	scene_tree_init(&scene->tree, NULL);

	wl_list_init(&scene->outputs);
	wl_list_init(&scene->presentation_destroy.link);

	const char *debug_damage_options[] = {
		"none",
		"rerender",
		"highlight",
		NULL
	};

	scene->debug_damage_option = env_parse_switch("WLR_SCENE_DEBUG_DAMAGE", debug_damage_options);
	scene->direct_scanout = !env_parse_bool("WLR_SCENE_DISABLE_DIRECT_SCANOUT");
	scene->calculate_visibility = !env_parse_bool("WLR_SCENE_DISABLE_VISIBILITY");

	return scene;
}

struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *parent) {
	assert(parent);

	struct wlr_scene_tree *tree = calloc(1, sizeof(struct wlr_scene_tree));
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
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each_reverse(child, &scene_tree->children, link) {
			if (_scene_nodes_in_box(child, box, iterator, user_data, lx + child->x, ly + child->y)) {
				return true;
			}
		}
		break;
	case WLR_SCENE_NODE_RECT:
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
	if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *scene_rect = scene_rect_from_node(node);
		if (scene_rect->color[3] != 1) {
			return;
		}
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		if (!scene_buffer->buffer) {
			return;
		}

		if (!buffer_is_opaque(scene_buffer->buffer)) {
			pixman_region32_copy(opaque, &scene_buffer->opaque_region);
			pixman_region32_translate(opaque, x, y);
			return;
		}
	}

	int width, height;
	scene_node_get_size(node, &width, &height);
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
		if (wlr_damage_ring_add(&scene_output->damage_ring, &output_damage)) {
			wlr_output_schedule_frame(scene_output->output);
		}
		pixman_region32_fini(&output_damage);
	}
}

static void update_node_update_outputs(struct wlr_scene_node *node,
		struct wl_list *outputs, struct wlr_scene_output *ignore) {
	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return;
	}

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

	uint32_t largest_overlap = 0;
	scene_buffer->primary_output = NULL;

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
		}

		pixman_region32_fini(&intersection);
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

	update_node_update_outputs(node, data->outputs, NULL);

	return false;
}

static void scene_node_visibility(struct wlr_scene_node *node,
		pixman_region32_t *visible) {
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
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
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
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
	struct wlr_scene_rect *scene_rect =
		calloc(1, sizeof(struct wlr_scene_rect));
	if (scene_rect == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_rect->node, WLR_SCENE_NODE_RECT, parent);

	scene_rect->width = width;
	scene_rect->height = height;
	memcpy(scene_rect->color, color, sizeof(scene_rect->color));

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

struct wlr_scene_buffer *wlr_scene_buffer_create(struct wlr_scene_tree *parent,
		struct wlr_buffer *buffer) {
	struct wlr_scene_buffer *scene_buffer = calloc(1, sizeof(*scene_buffer));
	if (scene_buffer == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_buffer->node, WLR_SCENE_NODE_BUFFER, parent);

	if (buffer) {
		scene_buffer->buffer = wlr_buffer_lock(buffer);
	}

	wl_signal_init(&scene_buffer->events.output_enter);
	wl_signal_init(&scene_buffer->events.output_leave);
	wl_signal_init(&scene_buffer->events.output_present);
	wl_signal_init(&scene_buffer->events.frame_done);
	pixman_region32_init(&scene_buffer->opaque_region);

	scene_node_update(&scene_buffer->node, NULL);

	return scene_buffer;
}

void wlr_scene_buffer_set_buffer_with_damage(struct wlr_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer, pixman_region32_t *damage) {
	// specifying a region for a NULL buffer doesn't make sense. We need to know
	// about the buffer to scale the buffer local coordinates down to scene
	// coordinates.
	assert(buffer || !damage);

	bool update = false;
	wlr_buffer_unlock(scene_buffer->buffer);

	wlr_texture_destroy(scene_buffer->texture);
	scene_buffer->texture = NULL;

	if (buffer) {
		// if this node used to not be mapped or its previous displayed
		// buffer region will be different from what the new buffer would
		// produce we need to update the node.
		update = !scene_buffer->buffer ||
			(scene_buffer->dst_width == 0 && scene_buffer->dst_height == 0 &&
				(scene_buffer->buffer->width != buffer->width ||
				scene_buffer->buffer->height != buffer->height));

		scene_buffer->buffer = wlr_buffer_lock(buffer);
	} else {
		update = true;
		scene_buffer->buffer = NULL;
	}

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
			(lx - scene_output->x) * output_scale,
			(ly - scene_output->y) * output_scale);
		if (wlr_damage_ring_add(&scene_output->damage_ring, &output_damage)) {
			wlr_output_schedule_frame(scene_output->output);
		}
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
		pixman_region32_t *region) {
	if (pixman_region32_equal(&scene_buffer->opaque_region, region)) {
		return;
	}

	pixman_region32_copy(&scene_buffer->opaque_region, region);
	scene_node_update(&scene_buffer->node, NULL);
}

void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *scene_buffer,
		const struct wlr_fbox *box) {
	struct wlr_fbox *cur = &scene_buffer->src_box;
	if ((wlr_fbox_empty(box) && wlr_fbox_empty(cur)) ||
			(box != NULL && wlr_fbox_equal(cur, box))) {
		return;
	}

	if (box != NULL) {
		memcpy(cur, box, sizeof(*box));
	} else {
		memset(cur, 0, sizeof(*cur));
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

static struct wlr_texture *scene_buffer_get_texture(
		struct wlr_scene_buffer *scene_buffer, struct wlr_renderer *renderer) {
	struct wlr_client_buffer *client_buffer =
		wlr_client_buffer_get(scene_buffer->buffer);
	if (client_buffer != NULL) {
		return client_buffer->texture;
	}

	if (scene_buffer->texture != NULL) {
		return scene_buffer->texture;
	}

	scene_buffer->texture =
		wlr_texture_from_buffer(renderer, scene_buffer->buffer);
	return scene_buffer->texture;
}

static void scene_node_get_size(struct wlr_scene_node *node,
		int *width, int *height) {
	*width = 0;
	*height = 0;

	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		return;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = scene_rect_from_node(node);
		*width = scene_rect->width;
		*height = scene_rect->height;
		break;
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		if (scene_buffer->dst_width > 0 && scene_buffer->dst_height > 0) {
			*width = scene_buffer->dst_width;
			*height = scene_buffer->dst_height;
		} else if (scene_buffer->buffer) {
			if (scene_buffer->transform & WL_OUTPUT_TRANSFORM_90) {
				*height = scene_buffer->buffer->width;
				*width = scene_buffer->buffer->height;
			} else {
				*width = scene_buffer->buffer->width;
				*height = scene_buffer->buffer->height;
			}
		}
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
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
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
				!scene_buffer->point_accepts_input(scene_buffer, rx, ry)) {
			return false;
		}
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

static void scissor_output(struct wlr_output *output, pixman_box32_t *rect) {
	struct wlr_renderer *renderer = output->renderer;
	assert(renderer);

	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};

	int ow, oh;
	wlr_output_transformed_resolution(output, &ow, &oh);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);
	wlr_box_transform(&box, &box, transform, ow, oh);

	wlr_renderer_scissor(renderer, &box);
}

static void render_rect(struct wlr_output *output,
		pixman_region32_t *damage, const float color[static 4],
		const struct wlr_box *box, const float matrix[static 9]) {
	struct wlr_renderer *renderer = output->renderer;
	assert(renderer);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_render_rect(renderer, box, color, matrix);
	}
}

static void render_texture(struct wlr_output *output,
		pixman_region32_t *damage, struct wlr_texture *texture,
		const struct wlr_fbox *src_box, const struct wlr_box *dst_box,
		const float matrix[static 9]) {
	struct wlr_renderer *renderer = output->renderer;
	assert(renderer);

	struct wlr_fbox default_src_box = {0};
	if (wlr_fbox_empty(src_box)) {
		default_src_box.width = texture->width;
		default_src_box.height = texture->height;
		src_box = &default_src_box;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_render_subtexture_with_matrix(renderer, texture, src_box, matrix, 1.0);
	}
}

static void scene_node_render(struct wlr_scene_node *node,
		struct wlr_scene_output *scene_output, pixman_region32_t *damage) {
	int x, y;
	wlr_scene_node_coords(node, &x, &y);
	x -= scene_output->x;
	y -= scene_output->y;

	struct wlr_output *output = scene_output->output;

	pixman_region32_t render_region;
	pixman_region32_init(&render_region);
	pixman_region32_copy(&render_region, &node->visible);
	pixman_region32_translate(&render_region, -scene_output->x, -scene_output->y);
	scale_output_damage(&render_region, output->scale);
	pixman_region32_intersect(&render_region, &render_region, damage);
	if (!pixman_region32_not_empty(&render_region)) {
		pixman_region32_fini(&render_region);
		return;
	}

	struct wlr_box dst_box = {
		.x = x,
		.y = y,
	};
	scene_node_get_size(node, &dst_box.width, &dst_box.height);
	scale_box(&dst_box, output->scale);

	struct wlr_texture *texture;
	float matrix[9];
	enum wl_output_transform transform;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		assert(false);
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = scene_rect_from_node(node);

		render_rect(output, &render_region, scene_rect->color, &dst_box,
			output->transform_matrix);
		break;
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		assert(scene_buffer->buffer);

		struct wlr_renderer *renderer = output->renderer;
		texture = scene_buffer_get_texture(scene_buffer, renderer);
		if (texture == NULL) {
			break;
		}

		transform = wlr_output_transform_invert(scene_buffer->transform);
		wlr_matrix_project_box(matrix, &dst_box, transform, 0.0,
			output->transform_matrix);

		render_texture(output, &render_region, texture, &scene_buffer->src_box,
			&dst_box, matrix);

		wl_signal_emit_mutable(&scene_buffer->events.output_present, scene_output);
		break;
	}

	pixman_region32_fini(&render_region);
}

static void scene_handle_presentation_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene *scene =
		wl_container_of(listener, scene, presentation_destroy);
	wl_list_remove(&scene->presentation_destroy.link);
	wl_list_init(&scene->presentation_destroy.link);
	scene->presentation = NULL;
}

void wlr_scene_set_presentation(struct wlr_scene *scene,
		struct wlr_presentation *presentation) {
	assert(scene->presentation == NULL);
	scene->presentation = presentation;
	scene->presentation_destroy.notify = scene_handle_presentation_destroy;
	wl_signal_add(&presentation->events.destroy, &scene->presentation_destroy);
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
		struct wl_list *outputs, struct wlr_scene_output *ignore) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_output_update(child, outputs, ignore);
		}
		return;
	}

	update_node_update_outputs(node, outputs, ignore);
}

static void scene_output_update_geometry(struct wlr_scene_output *scene_output) {
	int width, height;
	wlr_output_transformed_resolution(scene_output->output, &width, &height);
	wlr_damage_ring_set_bounds(&scene_output->damage_ring, width, height);
	wlr_output_schedule_frame(scene_output->output);

	scene_node_output_update(&scene_output->scene->tree.node,
			&scene_output->scene->outputs, NULL);
}

static void scene_output_handle_commit(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_commit);
	struct wlr_output_event_commit *event = data;

	if (event->committed & (WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_TRANSFORM |
			WLR_OUTPUT_STATE_SCALE |
			WLR_OUTPUT_STATE_ENABLED)) {
		scene_output_update_geometry(scene_output);
	}
}

static void scene_output_handle_mode(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_mode);
	scene_output_update_geometry(scene_output);
}

static void scene_output_handle_damage(struct wl_listener *listener, void *data) {
	struct wlr_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_damage);
	struct wlr_output_event_damage *event = data;
	if (wlr_damage_ring_add(&scene_output->damage_ring, event->damage)) {
		wlr_output_schedule_frame(scene_output->output);
	}
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

	scene_output->output_mode.notify = scene_output_handle_mode;
	wl_signal_add(&output->events.mode, &scene_output->output_mode);

	scene_output->output_damage.notify = scene_output_handle_damage;
	wl_signal_add(&output->events.damage, &scene_output->output_damage);

	scene_output->output_needs_frame.notify = scene_output_handle_needs_frame;
	wl_signal_add(&output->events.needs_frame, &scene_output->output_needs_frame);

	scene_output_update_geometry(scene_output);

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
		&scene_output->scene->outputs, scene_output);

	struct highlight_region *damage, *tmp_damage;
	wl_list_for_each_safe(damage, tmp_damage, &scene_output->damage_highlight_regions, link) {
		highlight_region_destroy(damage);
	}

	wlr_addon_finish(&scene_output->addon);
	wlr_damage_ring_finish(&scene_output->damage_ring);
	wl_list_remove(&scene_output->link);
	wl_list_remove(&scene_output->output_commit.link);
	wl_list_remove(&scene_output->output_mode.link);
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

	scene_output_update_geometry(scene_output);
}

static bool scene_node_invisible(struct wlr_scene_node *node) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		return true;
	} else if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = scene_rect_from_node(node);

		return rect->color[3] == 0.f;
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);

		return buffer->buffer == NULL;
	}

	return false;
}

struct render_list_constructor_data {
	struct wlr_box box;
	struct wl_array *render_list;
	bool calculate_visibility;
};

static bool construct_render_list_iterator(struct wlr_scene_node *node,
		int lx, int ly, void *_data) {
	struct render_list_constructor_data *data = _data;

	if (scene_node_invisible(node)) {
		return false;
	}

	// while rendering, the background should always be black.
	// If we see a black rect, we can ignore rendering everything under the rect
	// and even the rect itself.
	if (node->type == WLR_SCENE_NODE_RECT && data->calculate_visibility) {
		struct wlr_scene_rect *rect = scene_rect_from_node(node);
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

	struct wlr_scene_node **entry = wl_array_add(data->render_list,
		sizeof(struct wlr_scene_node *));
	if (entry) {
		*entry = node;
	}
	return false;
}

static bool scene_node_try_direct_scanout(struct wlr_scene_node *node,
		struct wlr_scene_output *scene_output, struct wlr_box *box) {
	if (!scene_output->scene->direct_scanout) {
		return false;
	}

	if (scene_output->scene->debug_damage_option ==
			WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		// We don't want to enter direct scan out if we have highlight regions
		// enabled. Otherwise, we won't be able to render the damage regions.
		return false;
	}

	if (node->type != WLR_SCENE_NODE_BUFFER) {
		return false;
	}

	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	struct wlr_output *output = scene_output->output;

	struct wlr_fbox default_box = {0};
	if (buffer->transform & WL_OUTPUT_TRANSFORM_90) {
		default_box.width = buffer->buffer->height;
		default_box.height = buffer->buffer->width;
	} else {
		default_box.width = buffer->buffer->width;
		default_box.height = buffer->buffer->height;
	}

	if (!wlr_fbox_empty(&buffer->src_box) &&
			!wlr_fbox_equal(&buffer->src_box, &default_box)) {
		return false;
	}

	if (buffer->transform != output->transform) {
		return false;
	}

	struct wlr_box node_box;
	wlr_scene_node_coords(node, &node_box.x, &node_box.y);
	scene_node_get_size(node, &node_box.width, &node_box.height);

	if (!wlr_box_equal(box, &node_box)) {
		return false;
	}

	wlr_output_attach_buffer(output, buffer->buffer);
	if (!wlr_output_test(output)) {
		wlr_output_rollback(output);
		return false;
	}

	return wlr_output_commit(output);
}

bool wlr_scene_output_commit(struct wlr_scene_output *scene_output) {
	struct wlr_output *output = scene_output->output;
	enum wlr_scene_debug_damage_option debug_damage =
		scene_output->scene->debug_damage_option;

	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	struct render_list_constructor_data list_con = {
		.box = { .x = scene_output->x, .y = scene_output->y },
		.render_list = &scene_output->render_list,
		.calculate_visibility = scene_output->scene->calculate_visibility,
	};
	wlr_output_effective_resolution(output,
		&list_con.box.width, &list_con.box.height);

	list_con.render_list->size = 0;
	scene_nodes_in_box(&scene_output->scene->tree.node, &list_con.box,
		construct_render_list_iterator, &list_con);
	array_realloc(list_con.render_list, list_con.render_list->size);

	int list_len = list_con.render_list->size / sizeof(struct wlr_scene_node *);
	struct wlr_scene_node **list_data = list_con.render_list->data;

	// if there is only one thing to render let's see if that thing can be
	// directly scanned out
	bool scanout = false;
	if (list_len == 1) {
		struct wlr_scene_node *node = list_data[0];
		scanout = scene_node_try_direct_scanout(node, scene_output, &list_con.box);
	}

	if (scene_output->prev_scanout != scanout) {
		scene_output->prev_scanout = scanout;
		wlr_log(WLR_DEBUG, "Direct scan-out %s",
			scanout ? "enabled" : "disabled");
		// When exiting direct scan-out, damage everything
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
	}

	if (scanout) {
		struct wlr_scene_node *node = list_data[0];

		assert(node->type == WLR_SCENE_NODE_BUFFER);
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
		wl_signal_emit_mutable(&buffer->events.output_present, scene_output);
		return true;
	}

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_RERENDER) {
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
	}

	struct timespec now;
	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct wl_list *regions = &scene_output->damage_highlight_regions;
		clock_gettime(CLOCK_MONOTONIC, &now);

		// add the current frame's damage if there is damage
		if (pixman_region32_not_empty(&scene_output->damage_ring.current)) {
			struct highlight_region *current_damage =
				calloc(1, sizeof(*current_damage));
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

		wlr_damage_ring_add(&scene_output->damage_ring, &acc_damage);
		pixman_region32_fini(&acc_damage);
	}

	int buffer_age;
	if (!wlr_output_attach_render(output, &buffer_age)) {
		return false;
	}

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	wlr_damage_ring_get_buffer_damage(&scene_output->damage_ring,
		buffer_age, &damage);
	if (!output->needs_frame && !pixman_region32_not_empty(
			&scene_output->damage_ring.current)) {
		pixman_region32_fini(&damage);
		wlr_output_rollback(output);
		return true;
	}

	wlr_renderer_begin(renderer, output->width, output->height);

	pixman_region32_t background;
	pixman_region32_init(&background);
	pixman_region32_copy(&background, &damage);

	// Cull areas of the background that are occluded by opaque regions of
	// scene nodes above. Those scene nodes will just render atop having us
	// never see the background.
	if (scene_output->scene->calculate_visibility) {
		float output_scale = scene_output->output->scale;

		for (int i = list_len - 1; i >= 0; i--) {
			struct wlr_scene_node *node = list_data[i];
			int x, y;
			wlr_scene_node_coords(node, &x, &y);

			// We must only cull opaque regions that are visible by the node.
			// The node's visibility will have the knowledge of a black rect
			// that may have been omitted from the render list via the black
			// rect optimization. In order to ensure we don't cull background
			// rendering in that black rect region, consider the node's visibility.
			pixman_region32_t opaque;
			pixman_region32_init(&opaque);
			scene_node_opaque_region(node, x, y, &opaque);
			pixman_region32_intersect(&opaque, &opaque, &node->visible);

			pixman_region32_translate(&opaque, -scene_output->x, -scene_output->y);
			wlr_region_scale(&opaque, &opaque, output_scale);
			pixman_region32_subtract(&background, &background, &opaque);
			pixman_region32_fini(&opaque);
		}

		if (floor(output_scale) != output_scale) {
			wlr_region_expand(&background, &background, 1);

			// reintersect with the damage because we never want to render
			// outside of the damage region
			pixman_region32_intersect(&background, &background, &damage);
		}
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&background, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_renderer_clear(renderer, (float[4]){ 0.0, 0.0, 0.0, 1.0 });
	}
	pixman_region32_fini(&background);

	for (int i = list_len - 1; i >= 0; i--) {
		struct wlr_scene_node *node = list_data[i];
		scene_node_render(node, scene_output, &damage);
	}

	wlr_renderer_scissor(renderer, NULL);

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct highlight_region *damage;
		wl_list_for_each(damage, &scene_output->damage_highlight_regions, link) {
			struct timespec time_diff;
			timespec_sub(&time_diff, &now, &damage->when);
			int64_t time_diff_ms = timespec_to_msec(&time_diff);
			float alpha = 1.0 - (double)time_diff_ms / HIGHLIGHT_DAMAGE_FADEOUT_TIME;

			int nrects;
			pixman_box32_t *rects = pixman_region32_rectangles(&damage->region, &nrects);
			for (int i = 0; i < nrects; ++i) {
				struct wlr_box box = {
					.x = rects[i].x1,
					.y = rects[i].y1,
					.width = rects[i].x2 - rects[i].x1,
					.height = rects[i].y2 - rects[i].y1,
				};

				float color[4] = { alpha * .5, 0.0, 0.0, alpha * .5 };
				wlr_render_rect(renderer, &box, color, output->transform_matrix);
			}
		}
	}

	wlr_output_render_software_cursors(output, &damage);

	wlr_renderer_end(renderer);
	pixman_region32_fini(&damage);

	int tr_width, tr_height;
	wlr_output_transformed_resolution(output, &tr_width, &tr_height);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);

	pixman_region32_t frame_damage;
	pixman_region32_init(&frame_damage);
	wlr_region_transform(&frame_damage,
		&scene_output->damage_ring.current,
		transform, tr_width, tr_height);
	wlr_output_set_damage(output, &frame_damage);
	pixman_region32_fini(&frame_damage);

	bool success = wlr_output_commit(output);

	if (success) {
		wlr_damage_ring_rotate(&scene_output->damage_ring);
	}

	if (debug_damage == WLR_SCENE_DEBUG_DAMAGE_HIGHLIGHT &&
			!wl_list_empty(&scene_output->damage_highlight_regions)) {
		wlr_output_schedule_frame(scene_output->output);
	}

	return success;
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
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
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
		struct wlr_scene_tree *scene_tree = scene_tree_from_node(node);
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
