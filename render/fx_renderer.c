#include "scenefx/config.h"

#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/render/interface.h>
#if SCENEFX_HAS_GLES2_RENDERER
#include <wlr/render/gles2.h>
#endif

#include "render/fx_renderer.h"
#include "render/tracy.h"
#include "scenefx/scenefx.h"
#if SCENEFX_HAS_GLES2_RENDERER
#include "render/gles2/gles2.h"
#endif

/* struct fx_renderer */
static struct wl_list fx_renderers = {
	.prev = &fx_renderers,
	.next = &fx_renderers,
};

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_renderer *fx_renderer = wl_container_of(listener, fx_renderer, renderer_destroy);

	wlr_log(WLR_INFO, "wlr_renderer being destroyed, destroying fx_renderer");

	TRACY_GPU_CONTEXT_DESTROY(fx_renderer);

	wl_list_remove(&fx_renderer->renderer_destroy.link);

	wl_list_remove(&fx_renderer->link);

	struct fx_offscreen_buffers *offscreen_buffers, *offscreen_buffers_tmp;
	wl_list_for_each_safe(offscreen_buffers, offscreen_buffers_tmp,
			&fx_renderer->offscreen_buffers, link) {
		fx_offscreen_buffers_destroy(offscreen_buffers);
	}

	wlr_drm_format_set_finish(fx_renderer->supported_drm_formats);
	free(fx_renderer->supported_drm_formats);

	fx_renderer->impl->renderer_destroy(fx_renderer);
}

/** Get the set of supported sampling and rendering DRM formats */
static struct wlr_drm_format_set *get_supported_drm_formats(struct wlr_renderer *wlr_renderer) {
	if (wlr_renderer->impl->get_render_formats == NULL) {
		return NULL;
	}

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer->impl->get_render_formats(wlr_renderer);
	const struct wlr_drm_format_set *texture_formats =
		wlr_renderer_get_texture_formats(wlr_renderer, wlr_renderer->render_buffer_caps);
	if (texture_formats == NULL || render_formats == NULL) {
		return NULL;
	}

	struct wlr_drm_format_set *formats = calloc(1, sizeof(*formats));

	// Find the format that supports both sampling and rendering
	bool result = wlr_drm_format_set_intersect(formats, texture_formats, render_formats);
	if (!result) {
		wlr_log(WLR_DEBUG, "Could not find suitable DRM format. "
				"Falling back to the sampling formats");
		free(formats);
		formats = NULL;
	}

	return formats;
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
		.tracy_data = NULL,
		.supported_drm_formats = get_supported_drm_formats(wlr_renderer),
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

struct fx_renderer *fx_renderer_try_from_wlr(struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *fx_renderer;
	wl_list_for_each(fx_renderer, &fx_renderers, link) {
		if (fx_renderer->wlr_renderer == wlr_renderer) {
			return fx_renderer;
		}
	}
	return NULL;
}

struct fx_renderer *scenefx_renderer_init_from_wlr(struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer);

#if !SCENEFX_HAS_ANY_RENDERERS
	wlr_log(WLR_ERROR,
			"Could not initialize SceneFX as it wasn't compiled with any renderers. "
			"Falling back to regular wlroots rendering.");
	return NULL;
#else
	struct fx_renderer *fx_renderer = fx_renderer_try_from_wlr(wlr_renderer);
	// Make sure that there's not an existing FX Renderer for the given wlr_renderer
	if (fx_renderer != NULL) {
		return fx_renderer;
	}

#if SCENEFX_HAS_GLES2_RENDERER
	if (wlr_renderer_is_gles2(wlr_renderer)) {
		fx_renderer = gles2_renderer_create(wlr_renderer);
		goto renderer_created;
	}
#endif
	// TODO: Add more renderers

	// No FX Renderer found for the given wlr_renderer type
	wlr_log(WLR_ERROR, "Could not initialize SceneFX: "
			"WLR_RENDERER must be set to a supported renderer!");
	return NULL;

renderer_created:
	if (fx_renderer == NULL) {
		wlr_log(WLR_ERROR, "Could not initialize SceneFX: "
				"Unable to create FX Renderer. "
				"Falling back to regular wlroots rendering.");
		return NULL;
	}

	// Insert the fx_renderer into the list of fx_renderers
	wl_list_insert(&fx_renderers, &fx_renderer->link);

	return fx_renderer;
#endif
}

struct wlr_renderer *scenefx_renderer_init(struct wlr_backend *backend) {
	struct wlr_renderer *wlr_renderer = wlr_renderer_autocreate(backend);
	if (wlr_renderer == NULL) {
		wlr_log(WLR_ERROR, "Could not create wlr_renderer");
		return NULL;
	}
	scenefx_renderer_init_from_wlr(wlr_renderer);
	return wlr_renderer;
}

struct fx_render_pass *fx_renderer_init_render_pass(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *wlr_render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output) {
	if (fx_renderer == NULL || fx_renderer->wlr_renderer != output->renderer) {
		return NULL;
	}
	return fx_renderer->impl->render_pass_allocate(fx_renderer, wlr_render_pass, wlr_buffer, output);
}
