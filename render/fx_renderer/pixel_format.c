#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "render/fx_renderer/fx_renderer.h"
#include "render/pixel_format.h"

/*
 * The DRM formats are little endian while the GL formats are big endian,
 * so DRM_FORMAT_ARGB8888 is actually compatible with GL_BGRA_EXT.
 */
static const struct fx_pixel_format formats[] = {
	{
		.drm_format = DRM_FORMAT_ARGB8888,
		.gl_format = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.drm_format = DRM_FORMAT_XRGB8888,
		.gl_format = GL_BGRA_EXT,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.drm_format = DRM_FORMAT_XBGR8888,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.drm_format = DRM_FORMAT_ABGR8888,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_BYTE,
	},
	{
		.drm_format = DRM_FORMAT_BGR888,
		.gl_format = GL_RGB,
		.gl_type = GL_UNSIGNED_BYTE,
	},
#if WLR_LITTLE_ENDIAN
	{
		.drm_format = DRM_FORMAT_RGBX4444,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
	},
	{
		.drm_format = DRM_FORMAT_RGBA4444,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_4_4_4_4,
	},
	{
		.drm_format = DRM_FORMAT_RGBX5551,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
	},
	{
		.drm_format = DRM_FORMAT_RGBA5551,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT_5_5_5_1,
	},
	{
		.drm_format = DRM_FORMAT_RGB565,
		.gl_format = GL_RGB,
		.gl_type = GL_UNSIGNED_SHORT_5_6_5,
	},
	{
		.drm_format = DRM_FORMAT_XBGR2101010,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
	},
	{
		.drm_format = DRM_FORMAT_ABGR2101010,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_INT_2_10_10_10_REV_EXT,
	},
	{
		.drm_format = DRM_FORMAT_XBGR16161616F,
		.gl_format = GL_RGBA,
		.gl_type = GL_HALF_FLOAT_OES,
	},
	{
		.drm_format = DRM_FORMAT_ABGR16161616F,
		.gl_format = GL_RGBA,
		.gl_type = GL_HALF_FLOAT_OES,
	},
	{
		.drm_format = DRM_FORMAT_XBGR16161616,
		.gl_internalformat = GL_RGBA16_EXT,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT,
	},
	{
		.drm_format = DRM_FORMAT_ABGR16161616,
		.gl_internalformat = GL_RGBA16_EXT,
		.gl_format = GL_RGBA,
		.gl_type = GL_UNSIGNED_SHORT,
	},
#endif
};

// TODO: more pixel formats

/*
 * Return true if supported for texturing, even if other operations like
 * reading aren't supported.
 */
bool is_fx_pixel_format_supported(const struct fx_renderer *renderer,
		const struct fx_pixel_format *format) {
	if (format->gl_type == GL_UNSIGNED_INT_2_10_10_10_REV_EXT
			&& !renderer->exts.EXT_texture_type_2_10_10_10_REV) {
		return false;
	}
	if (format->gl_type == GL_HALF_FLOAT_OES
			&& !renderer->exts.OES_texture_half_float_linear) {
		return false;
	}
	if (format->gl_type == GL_UNSIGNED_SHORT
			&& !renderer->exts.EXT_texture_norm16) {
		return false;
	}
	/*
	 * Note that we don't need to check for GL_EXT_texture_format_BGRA8888
	 * here, since we've already checked if we have it at renderer creation
	 * time and bailed out if not. We do the check there because Wayland
	 * requires all compositors to support SHM buffers in that format.
	 */
	return true;
}

const struct fx_pixel_format *get_fx_format_from_drm(uint32_t fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].drm_format == fmt) {
			return &formats[i];
		}
	}
	return NULL;
}

const struct fx_pixel_format *get_fx_format_from_gl(
		GLint gl_format, GLint gl_type, bool alpha) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].gl_format != gl_format ||
				formats[i].gl_type != gl_type) {
			continue;
		}

		if (pixel_format_has_alpha(formats[i].drm_format) != alpha) {
			continue;
		}

		return &formats[i];
	}
	return NULL;
}

void get_fx_shm_formats(const struct fx_renderer *renderer,
		struct wlr_drm_format_set *out) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		if (!is_fx_pixel_format_supported(renderer, &formats[i])) {
			continue;
		}
		wlr_drm_format_set_add(out, formats[i].drm_format, DRM_FORMAT_MOD_INVALID);
		wlr_drm_format_set_add(out, formats[i].drm_format, DRM_FORMAT_MOD_LINEAR);
	}
}
