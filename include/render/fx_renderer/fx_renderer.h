#ifndef _FX_OPENGL_H
#define _FX_OPENGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <time.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

#include "render/fx_renderer/shaders.h"
#include "render/pass.h"
#include "scenefx/types/fx/shadow_data.h"

struct fx_pixel_format {
	uint32_t drm_format;
	// optional field, if empty then internalformat = format
	GLint gl_internalformat;
	GLint gl_format, gl_type;
	bool has_alpha;
};

bool is_fx_pixel_format_supported(const struct fx_renderer *renderer,
	const struct fx_pixel_format *format);
const struct fx_pixel_format *get_fx_format_from_drm(uint32_t fmt);
const struct fx_pixel_format *get_fx_format_from_gl(
	GLint gl_format, GLint gl_type, bool alpha);
const uint32_t *get_fx_shm_formats(const struct fx_renderer *renderer,
	size_t *len);

///
/// fx_framebuffer
///

struct fx_framebuffer {
	struct wlr_buffer *buffer;
	struct fx_renderer *renderer;
	struct wl_list link; // fx_renderer.buffers

	EGLImageKHR image;
	GLuint rbo;
	GLuint fbo;
	GLuint sb; // Stencil

	struct wlr_addon addon;
};

/** Should only be used with custom fbs */
void fx_framebuffer_get_or_create_bufferless(struct fx_renderer *fx_renderer,
		struct wlr_output *output, struct fx_framebuffer **fx_buffer);

struct fx_framebuffer *fx_framebuffer_get_or_create(struct fx_renderer *renderer,
		struct wlr_buffer *wlr_buffer);

void fx_framebuffer_bind(struct fx_framebuffer *buffer);

void fx_framebuffer_bind_wlr_fbo(struct fx_renderer *renderer);

void fx_framebuffer_destroy(struct fx_framebuffer *buffer);

///
/// fx_texture
///

struct fx_texture {
	struct wlr_texture wlr_texture;
	struct fx_renderer *fx_renderer;
	struct wl_list link; // fx_renderer.textures

	// Basically:
	//   GL_TEXTURE_2D == mutable
	//   GL_TEXTURE_EXTERNAL_OES == immutable
	GLuint target;
	GLuint tex;

	EGLImageKHR image;

	bool has_alpha;

	// Only affects target == GL_TEXTURE_2D
	uint32_t drm_format; // used to interpret upload data
	// If imported from a wlr_buffer
	struct wlr_buffer *buffer;
	struct wlr_addon buffer_addon;
};

struct fx_texture_attribs {
	GLenum target; /* either GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES */
	GLuint tex;

	bool has_alpha;
};

struct fx_texture *fx_get_texture(struct wlr_texture *wlr_texture);

struct wlr_texture *fx_texture_from_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *buffer);

void fx_texture_destroy(struct fx_texture *texture);

bool wlr_texture_is_fx(struct wlr_texture *wlr_texture);

void fx_texture_get_attribs(struct wlr_texture *texture,
	struct fx_texture_attribs *attribs);

///
/// fx_renderer
///

struct fx_renderer {
	struct wlr_renderer wlr_renderer;

	float projection[9];
	struct wlr_egl *egl;
	int drm_fd;

	const char *exts_str;
	struct {
		bool EXT_read_format_bgra;
		bool KHR_debug;
		bool OES_egl_image_external;
		bool OES_egl_image;
		bool EXT_texture_type_2_10_10_10_REV;
		bool OES_texture_half_float_linear;
		bool EXT_texture_norm16;
		bool EXT_disjoint_timer_query;
	} exts;

	struct {
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
		PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
		PFNGLDEBUGMESSAGECONTROLKHRPROC glDebugMessageControlKHR;
		PFNGLPOPDEBUGGROUPKHRPROC glPopDebugGroupKHR;
		PFNGLPUSHDEBUGGROUPKHRPROC glPushDebugGroupKHR;
		PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES;
		PFNGLGETGRAPHICSRESETSTATUSKHRPROC glGetGraphicsResetStatusKHR;
		PFNGLGENQUERIESEXTPROC glGenQueriesEXT;
		PFNGLDELETEQUERIESEXTPROC glDeleteQueriesEXT;
		PFNGLQUERYCOUNTEREXTPROC glQueryCounterEXT;
		PFNGLGETQUERYOBJECTIVEXTPROC glGetQueryObjectivEXT;
		PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT;
		PFNGLGETINTEGER64VEXTPROC glGetInteger64vEXT;
	} procs;

	struct shaders shaders;

	struct wl_list buffers; // fx_framebuffer.link
	struct wl_list textures; // fx_texture.link

	struct fx_framebuffer *current_buffer;
	uint32_t viewport_width, viewport_height;

	// Contains the blurred background for tiled windows
	struct fx_framebuffer *optimized_blur_buffer;
	// Contains the original pixels to draw over the areas where artifact are visible
	struct fx_framebuffer *blur_saved_pixels_buffer;
	// Blur swaps between the two effects buffers everytime it scales the image
	// Buffer used for effects
	struct fx_framebuffer *effects_buffer;
	// Swap buffer used for effects
	struct fx_framebuffer *effects_buffer_swapped;

	// The region where there's blur
	pixman_region32_t blur_padding_region;

	bool blur_buffer_dirty;
};

bool wlr_renderer_is_fx(struct wlr_renderer *wlr_renderer);

struct fx_renderer *fx_get_renderer(
	struct wlr_renderer *wlr_renderer);
struct fx_render_timer *fx_get_render_timer(
	struct wlr_render_timer *timer);
struct fx_texture *fx_get_texture(
	struct wlr_texture *wlr_texture);

struct wlr_renderer *fx_renderer_create_egl(struct wlr_egl *egl);

struct wlr_egl *wlr_fx_renderer_get_egl(struct wlr_renderer *renderer);

void push_fx_debug_(struct fx_renderer *renderer,
	const char *file, const char *func);
#define push_fx_debug(renderer) push_fx_debug_(renderer, _WLR_FILENAME, __func__)
void pop_fx_debug(struct fx_renderer *renderer);

///
/// Render Timer
///

struct fx_render_timer {
	struct wlr_render_timer base;
	struct fx_renderer *renderer;
	struct timespec cpu_start;
	struct timespec cpu_end;
	GLuint id;
	GLint64 gl_cpu_end;
};

bool wlr_render_timer_is_fx(struct wlr_render_timer *timer);

#endif
