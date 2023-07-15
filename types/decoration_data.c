#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/decoration_data.h"
#include "wlr/util/log.h"

struct decoration_data decoration_data_get_undecorated(void) {
	return (struct decoration_data) {
		.alpha = 1.0f,
		.dim = 0.0f,
		.dim_color = default_dim_color,
		.corner_radius = 0,
		.saturation = 1.0f,
		.has_titlebar = false,
		.blur = false,
		.shadow = false,
	};
}

static void decoration_data_handle_destroy(struct wlr_addon *addon) {
	struct decoration_data *deco_data = wl_container_of(addon, deco_data, addon);
	free(deco_data);
}

static const struct wlr_addon_interface decoration_data_addon_impl = {
	.name = "decoration_data",
	.destroy = decoration_data_handle_destroy,
};

void wlr_scene_tree_decoration_data_init(struct wlr_scene_node *scene_node,
		struct decoration_data decoration_data) {
	if (!scene_node) {
		wlr_log(WLR_ERROR, "wlr_scene_node is NULL, cannot add decoration_data");
		return;
	}

	struct decoration_data *deco_data = malloc(sizeof(struct decoration_data));
	memcpy(deco_data, &decoration_data, sizeof(struct decoration_data));

	wlr_addon_init(&deco_data->addon, &scene_node->addons, scene_node,
			&decoration_data_addon_impl);
}

struct decoration_data *wlr_scene_tree_decoration_data_get(
		struct wlr_scene_tree *scene_tree) {
	/* Get the client tree */
	struct wlr_scene_node *node = &scene_tree->node;
	struct wlr_addon *addon = wlr_addon_find(&node->addons,
			scene_tree, &decoration_data_addon_impl);
	if (!addon) {
		return NULL;
	}

	struct decoration_data *deco_data = wl_container_of(addon, deco_data, addon);
	return deco_data;
}
