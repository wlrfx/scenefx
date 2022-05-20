#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_presentation_time.h>
#include "types/wlr_scene.h"

static void handle_scene_buffer_output_enter(
		struct wl_listener *listener, void *data) {
	struct wlr_scene_surface *surface =
		wl_container_of(listener, surface, output_enter);
	struct wlr_scene_output *output = data;

	wlr_surface_send_enter(surface->surface, output->output);
}

static void handle_scene_buffer_output_leave(
		struct wl_listener *listener, void *data) {
	struct wlr_scene_surface *surface =
		wl_container_of(listener, surface, output_leave);
	struct wlr_scene_output *output = data;

	wlr_surface_send_leave(surface->surface, output->output);
}

static void handle_scene_buffer_output_present(
		struct wl_listener *listener, void *data) {
	struct wlr_scene_surface *surface =
		wl_container_of(listener, surface, output_present);
	struct wlr_scene_output *scene_output = data;

	if (surface->buffer->primary_output == scene_output) {
		struct wlr_scene *root = scene_node_get_root(&surface->buffer->node);
		struct wlr_presentation *presentation = root->presentation;

		if (presentation) {
			wlr_presentation_surface_sampled_on_output(
				presentation, surface->surface, scene_output->output);
		}
	}
}

static void handle_scene_buffer_frame_done(
		struct wl_listener *listener, void *data) {
	struct wlr_scene_surface *surface =
		wl_container_of(listener, surface, frame_done);
	struct timespec *now = data;

	wlr_surface_send_frame_done(surface->surface, now);
}

static void scene_surface_handle_surface_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_scene_surface *surface =
		wl_container_of(listener, surface, surface_destroy);

	wlr_scene_node_destroy(&surface->buffer->node);
}

static void set_buffer_with_surface_state(struct wlr_scene_buffer *scene_buffer,
		struct wlr_surface *surface) {
	struct wlr_surface_state *state = &surface->current;

	wlr_scene_buffer_set_opaque_region(scene_buffer, &surface->opaque_region);

	struct wlr_fbox src_box;
	wlr_surface_get_buffer_source_box(surface, &src_box);
	wlr_scene_buffer_set_source_box(scene_buffer, &src_box);

	wlr_scene_buffer_set_dest_size(scene_buffer, state->width, state->height);
	wlr_scene_buffer_set_transform(scene_buffer, state->transform);

	if (surface->buffer) {
		wlr_scene_buffer_set_buffer_with_damage(scene_buffer,
			&surface->buffer->base, &surface->buffer_damage);
	} else {
		wlr_scene_buffer_set_buffer(scene_buffer, NULL);
	}
}

static void handle_scene_surface_surface_commit(
		struct wl_listener *listener, void *data) {
	struct wlr_scene_surface *surface =
		wl_container_of(listener, surface, surface_commit);
	struct wlr_scene_buffer *scene_buffer = surface->buffer;

	set_buffer_with_surface_state(scene_buffer, surface->surface);

	// Even if the surface hasn't submitted damage, schedule a new frame if
	// the client has requested a wl_surface.frame callback. Check if the node
	// is visible. If not, the client will never receive a frame_done event
	// anyway so it doesn't make sense to schedule here.
	int lx, ly;
	bool enabled = wlr_scene_node_coords(&scene_buffer->node, &lx, &ly);

	if (!wl_list_empty(&surface->surface->current.frame_callback_list) &&
			surface->buffer->primary_output != NULL && enabled) {
		wlr_output_schedule_frame(surface->buffer->primary_output->output);
	}
}

static bool scene_buffer_point_accepts_input(struct wlr_scene_buffer *scene_buffer,
		int sx, int sy) {
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_from_buffer(scene_buffer);

	return wlr_surface_point_accepts_input(scene_surface->surface, sx, sy);
}

static void surface_addon_destroy(struct wlr_addon *addon) {
	struct wlr_scene_surface *surface = wl_container_of(addon, surface, addon);

	wlr_addon_finish(&surface->addon);

	wl_list_remove(&surface->output_enter.link);
	wl_list_remove(&surface->output_leave.link);
	wl_list_remove(&surface->output_present.link);
	wl_list_remove(&surface->frame_done.link);
	wl_list_remove(&surface->surface_destroy.link);
	wl_list_remove(&surface->surface_commit.link);

	free(surface);
}

static const struct wlr_addon_interface surface_addon_impl = {
	.name = "wlr_scene_surface",
	.destroy = surface_addon_destroy,
};

struct wlr_scene_surface *wlr_scene_surface_from_buffer(
		struct wlr_scene_buffer *scene_buffer) {
	struct wlr_addon *addon = wlr_addon_find(&scene_buffer->node.addons,
		scene_buffer, &surface_addon_impl);
	if (!addon) {
		return NULL;
	}

	struct wlr_scene_surface *surface = wl_container_of(addon, surface, addon);
	return surface;
}

struct wlr_scene_surface *wlr_scene_surface_create(struct wlr_scene_tree *parent,
		struct wlr_surface *wlr_surface) {
	struct wlr_scene_surface *surface = calloc(1, sizeof(*surface));
	if (surface == NULL) {
		return NULL;
	}

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_create(parent, NULL);
	if (!scene_buffer) {
		free(surface);
		return NULL;
	}

	surface->buffer = scene_buffer;
	surface->surface = wlr_surface;
	scene_buffer->point_accepts_input = scene_buffer_point_accepts_input;

	surface->output_enter.notify = handle_scene_buffer_output_enter;
	wl_signal_add(&scene_buffer->events.output_enter, &surface->output_enter);

	surface->output_leave.notify = handle_scene_buffer_output_leave;
	wl_signal_add(&scene_buffer->events.output_leave, &surface->output_leave);

	surface->output_present.notify = handle_scene_buffer_output_present;
	wl_signal_add(&scene_buffer->events.output_present, &surface->output_present);

	surface->frame_done.notify = handle_scene_buffer_frame_done;
	wl_signal_add(&scene_buffer->events.frame_done, &surface->frame_done);

	surface->surface_destroy.notify = scene_surface_handle_surface_destroy;
	wl_signal_add(&wlr_surface->events.destroy, &surface->surface_destroy);

	surface->surface_commit.notify = handle_scene_surface_surface_commit;
	wl_signal_add(&wlr_surface->events.commit, &surface->surface_commit);

	wlr_addon_init(&surface->addon, &scene_buffer->node.addons,
		scene_buffer, &surface_addon_impl);

	set_buffer_with_surface_state(scene_buffer, wlr_surface);

	return surface;
}
