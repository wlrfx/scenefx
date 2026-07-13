#include <drm_fourcc.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/wlr_renderer.h>

#include "render/fx_renderer.h"

static const uint32_t formats_alpha[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
#if WLR_LITTLE_ENDIAN
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_ABGR16161616F,
	DRM_FORMAT_ABGR16161616,
#endif
};

static const uint32_t formats_opaque[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
#if WLR_LITTLE_ENDIAN
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_XBGR16161616,
	DRM_FORMAT_BGR161616F,
	DRM_FORMAT_BGR161616,
#endif
};

// TODO: more pixel formats

static const size_t formats_alpha_size =
	sizeof(formats_alpha) / sizeof(formats_alpha[0]);

static const size_t formats_opaque_size =
	sizeof(formats_opaque) / sizeof(formats_opaque[0]);

const struct wlr_drm_format *find_wlr_drm_format(struct wlr_renderer *wlr_renderer, bool alpha) {
	const struct wlr_drm_format_set *texture_formats = wlr_renderer_get_texture_formats(
			wlr_renderer, wlr_renderer->render_buffer_caps);

	const uint32_t *formats = alpha ? formats_alpha : formats_opaque;
	const size_t formats_size = alpha ? formats_alpha_size : formats_opaque_size;

	for (size_t i = 0; i < formats_size; ++i) {
		const struct wlr_drm_format *format = wlr_drm_format_set_get(texture_formats, formats[i]);
		if (format != NULL) {
			return format;
		}
	}
	return NULL;
}
