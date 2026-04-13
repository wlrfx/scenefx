#include <assert.h>
#include <wlr/config.h>
#if WLR_HAS_GLES2_RENDERER
#include <wlr/render/gles2.h>
#endif
#include <wlr/util/log.h>
#include <wlr/render/interface.h>

#include "render/fx_renderer.h"
#if WLR_HAS_GLES2_RENDERER
#include "render/gles2/gles2.h"
#endif
#include "render/tracy.h"
#include "scenefx/scenefx.h"

static void scene_addon_handle_destroy(struct wlr_addon *addon) {
	struct fx_renderer *fx_renderer = wl_container_of(addon, fx_renderer, scene_addon);
	wlr_addon_finish(&fx_renderer->scene_addon);
	fx_renderer->scene_addon_attached = false;
}

static const struct wlr_addon_interface fx_renderer_scene_addon_impl = {
	.name = "fx_renderer",
	.destroy = scene_addon_handle_destroy,
};

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_renderer *fx_renderer = wl_container_of(listener, fx_renderer, renderer_destroy);

	wlr_log(WLR_INFO, "wlr_renderer being destroyed, destroying fx_renderer");

	TRACY_GPU_CONTEXT_DESTROY(fx_renderer);

	wl_list_remove(&fx_renderer->renderer_destroy.link);

	if (fx_renderer->scene_addon_attached) {
		scene_addon_handle_destroy(&fx_renderer->scene_addon);
	}

	struct fx_offscreen_buffers *offscreen_buffers, *offscreen_buffers_tmp;
	wl_list_for_each_safe(offscreen_buffers, offscreen_buffers_tmp,
			&fx_renderer->offscreen_buffers, link) {
		fx_offscreen_buffers_destroy(offscreen_buffers);
	}

	fx_renderer->impl->renderer_destroy(fx_renderer);
}

void fx_renderer_init(struct fx_renderer *fx_renderer,
		const struct fx_renderer_impl *impl, struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer);
	assert(impl->offscreen_buffers_allocate);
	assert(impl->render_pass_allocate);
	assert(impl->renderer_destroy);

	*fx_renderer = (struct fx_renderer) {
		.scene_addon_attached = false,
		.impl = impl,
		.wlr_renderer = wlr_renderer,
		.tracy_data = NULL,
	};

#ifdef TRACY_ENABLE
	if (impl->tracy) {
		fx_renderer->tracy_data = TRACY_GPU_CONTEXT_NEW(fx_renderer);
	}
#endif

	fx_renderer->renderer_destroy.notify = handle_renderer_destroy;
	wl_signal_add(&fx_renderer->wlr_renderer->events.destroy, &fx_renderer->renderer_destroy);

	wl_list_init(&fx_renderer->offscreen_buffers);
}

struct wlr_renderer *fx_renderer_get_wlr_renderer(struct fx_renderer *fx_renderer) {
	if (fx_renderer == NULL) {
		return NULL;
	}
	return fx_renderer->wlr_renderer;
}

struct fx_renderer *scenefx_find_fx_renderer(struct wlr_scene *scene,
		struct wlr_renderer *wlr_renderer) {
	if (scene == NULL) {
		return NULL;
	}

	struct wlr_addon *addon = wlr_addon_find(&scene->tree.node.addons, wlr_renderer,
			&fx_renderer_scene_addon_impl);
	if (!addon) {
		return NULL;
	}

	struct fx_renderer *fx_renderer = wl_container_of(addon, fx_renderer, scene_addon);
	return fx_renderer;
}

struct fx_renderer *scenefx_init_complete(struct wlr_scene *wlr_scene, struct wlr_backend *backend) {
	if (wlr_scene == NULL) {
		wlr_log(WLR_ERROR, "Could not initialize scenefx: wlr_scene is NULL");
		return NULL;
	}

	struct wlr_renderer *wlr_renderer = wlr_renderer_autocreate(backend);
	if (wlr_renderer == NULL) {
		wlr_log(WLR_ERROR, "Could not create wlr_renderer");
		return NULL;
	}

	struct fx_renderer *fx_renderer = NULL;
#if WLR_HAS_GLES2_RENDERER
	if (wlr_renderer_is_gles2(wlr_renderer)) {
		fx_renderer = gles2_renderer_create(wlr_renderer);
		goto renderer_created;
	}
#endif
	// TODO: Add more renderers

renderer_created:
	if (fx_renderer == NULL) {
		wlr_log(WLR_ERROR, "Could not initialize SceneFX. WLR_RENDERER must be set to a supported renderer");
		wlr_renderer_destroy(wlr_renderer);
		return NULL;
	}

	// Add the fx_renderer to the wlr_scene addon set, but ensure that the
	// wlr_renderer is the owner as there could be multiple fx_renderers.
	wlr_addon_init(&fx_renderer->scene_addon, &wlr_scene->tree.node.addons,
			wlr_renderer, &fx_renderer_scene_addon_impl);
	fx_renderer->scene_addon_attached = true;

	return fx_renderer;
}

struct wlr_renderer *scenefx_init(struct wlr_scene *wlr_scene, struct wlr_backend *backend) {
	struct fx_renderer *fx_renderer = scenefx_init_complete(wlr_scene, backend);
	return fx_renderer_get_wlr_renderer(fx_renderer);
}

struct fx_render_pass *fx_renderer_init_render_pass(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *wlr_render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output) {
	if (fx_renderer == NULL || fx_renderer->wlr_renderer != output->renderer) {
		return NULL;
	}
	return fx_renderer->impl->render_pass_allocate(fx_renderer, wlr_render_pass, wlr_buffer, output);
}
