#include <assert.h>
#include <stdlib.h>
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

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_renderer *fx_renderer = wl_container_of(listener, fx_renderer, renderer_destroy);

	wlr_log(WLR_INFO, "wlr_renderer being destroyed, destroying fx_renderer");

	TRACY_GPU_CONTEXT_DESTROY(fx_renderer->tracy_data);

	wl_list_remove(&fx_renderer->renderer_destroy.link);

	struct fx_offscreen_buffers *offscreen_buffers, *offscreen_buffers_tmp;
	wl_list_for_each_safe(offscreen_buffers, offscreen_buffers_tmp,
			&fx_renderer->offscreen_buffers, link) {
		fx_offscreen_buffers_destroy(offscreen_buffers);
	}

	fx_renderer->impl->renderer_destroy(fx_renderer);

	fx_renderer->wlr_renderer = NULL;
	free(fx_renderer);
}

void fx_renderer_init(struct fx_renderer *fx_renderer,
		const struct fx_renderer_impl *impl, struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer);
	assert(impl->offscreen_buffers_allocate);
	assert(impl->render_pass_allocate);
	assert(impl->renderer_destroy);

	*fx_renderer = (struct fx_renderer) {
		.impl = impl,
		.wlr_renderer = wlr_renderer,
#ifdef TRACY_ENABLE
		.tracy_data = NULL,
#endif
	};

#ifdef TRACY_ENABLE
	fx_renderer->tracy_capable = impl->tracy_gpu_zone_begin
		&& impl->tracy_gpu_zone_end
		&& impl->tracy_gpu_context_collect
		&& impl->tracy_gpu_context_destroy
		&& impl->tracy_gpu_context_new;
	fx_renderer->tracy_data = TRACY_GPU_CONTEXT_NEW(fx_renderer);
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

struct fx_renderer *scenefx_init_complete(struct wlr_scene *wlr_scene, struct wlr_backend *backend) {
	wlr_scene->fx_renderer = NULL;

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

	wlr_scene->fx_renderer = fx_renderer;
	return fx_renderer;
}

struct wlr_renderer *scenefx_init(struct wlr_scene *wlr_scene, struct wlr_backend *backend) {
	struct fx_renderer *fx_renderer = scenefx_init_complete(wlr_scene, backend);
	return fx_renderer_get_wlr_renderer(fx_renderer);
}
