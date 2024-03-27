#ifndef _FX_OPENGL_H
#define _FX_OPENGL_H

#include <GLES2/gl2.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <stdbool.h>
#include <time.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

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
void fx_framebuffer_get_or_create_custom(struct fx_renderer *fx_renderer,
		struct wlr_output *output, struct fx_framebuffer **fx_buffer);

struct fx_framebuffer *fx_framebuffer_get_or_create(struct fx_renderer *renderer,
		struct wlr_buffer *wlr_buffer);

void fx_framebuffer_bind(struct fx_framebuffer *buffer);

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

struct fx_texture *fx_get_texture(struct wlr_texture *wlr_texture);

void fx_texture_destroy(struct fx_texture *texture);

bool wlr_texture_is_fx(struct wlr_texture *wlr_texture);

bool wlr_renderer_is_fx(struct wlr_renderer *wlr_renderer);

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
