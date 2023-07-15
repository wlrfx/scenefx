#ifndef TYPES_SUBSURFACE_TREE_H
#define TYPES_SUBSURFACE_TREE_H

#include <types/wlr_scene.h>
#include <wayland-server-core.h>
#include <wlr/util/addon.h>

/**
 * A tree for a surface and all of its child sub-surfaces.
 *
 * `tree` contains `scene_surface` and one node per sub-surface.
 */
struct wlr_scene_subsurface_tree {
	struct wlr_scene_tree *tree;
	struct wlr_surface *surface;
	struct wlr_scene_surface *scene_surface;

	struct wl_listener tree_destroy;
	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
	struct wl_listener surface_new_subsurface;

	struct wlr_scene_subsurface_tree *parent; // NULL for the top-level surface

	// Only valid if the surface is a sub-surface

	struct wlr_addon surface_addon;

	struct wl_listener subsurface_destroy;
	struct wl_listener subsurface_map;
	struct wl_listener subsurface_unmap;
};

#endif
