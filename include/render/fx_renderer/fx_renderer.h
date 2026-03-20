#ifndef _FX_RENDERER_H
#define _FX_RENDERER_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wlr/render/egl.h>
#include <wlr/render/allocator.h>
#include <wlr/util/addon.h>

#include "render/fx_renderer/shaders.h"
#include "render/tracy.h"

struct fx_framebuffer;

/** Find the best supported DRM format */
const struct wlr_drm_format *find_wlr_drm_format(struct wlr_renderer *wlr_renderer, bool alpha);

///
/// fx_framebuffer
///

struct fx_framebuffer {
	struct wlr_buffer *wlr_buffer;
	struct fx_renderer *fx_renderer;
	struct wl_list link; // fx_renderer.buffers
	GLuint sb; // Stencil

	// fbo allocated by wlroots, don't destroy!
	GLuint fbo;

	struct wlr_addon addon;
};

/**
 * Should only be used with custom fbs.
 * Note: Does not bind back to the default Framebuffer!
 */
void fx_framebuffer_get_or_create_custom(struct fx_renderer *fx_renderer,
		struct wlr_allocator *allocator, int width, int height, bool has_alpha,
		struct fx_framebuffer **fx_buffer, bool *failed);

struct fx_framebuffer *fx_framebuffer_get_or_create(struct fx_renderer *fx_renderer,
		struct wlr_buffer *wlr_buffer);

void fx_framebuffer_bind(struct fx_framebuffer *buffer);

void fx_framebuffer_destroy(struct fx_framebuffer *buffer);

void push_fx_debug_(struct fx_renderer *renderer, const char *file, const char *func);

#define push_fx_debug(renderer) push_fx_debug_(renderer, _WLR_FILENAME, __func__)

void pop_fx_debug(struct fx_renderer *renderer);

///
/// fx_renderer
///

/**
 * wlroots OpenGL ES 2 renderer wrapper.
 */

// TODO: Make more generic and move the current fx_renderer -> fx_gles2_renderer
// to allow for a future fx_vk_renderer
struct fx_renderer {
	struct wlr_renderer *wlr_renderer;
	struct wlr_egl *wlr_egl;

	struct {
		bool OES_egl_image_external;
		bool EXT_disjoint_timer_query;
	} exts;

	struct {
		PFNGLPOPDEBUGGROUPKHRPROC glPopDebugGroupKHR;
		PFNGLPUSHDEBUGGROUPKHRPROC glPushDebugGroupKHR;
		PFNGLGENQUERIESEXTPROC glGenQueriesEXT;
		PFNGLDELETEQUERIESEXTPROC glDeleteQueriesEXT;
		PFNGLQUERYCOUNTEREXTPROC glQueryCounterEXT;
		PFNGLGETQUERYOBJECTIVEXTPROC glGetQueryObjectivEXT;
		PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT;
		PFNGLGETINTEGER64VEXTPROC glGetInteger64vEXT;
		TRACY_FN(
			PFNGLGETQUERYIVEXTPROC glGetQueryivEXT;
		)
	} gl_procs;

	struct {
		PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
		PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
		PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR;
	} egl_procs;

	struct {
		struct quad_shader quad;
		struct quad_shader quad_clip;
		struct quad_grad_shader quad_grad;
		struct quad_round_shader quad_round;
		struct quad_grad_round_shader quad_grad_round;

		struct tex_shader tex_rgba;
		struct tex_shader tex_rgbx;
		struct tex_shader tex_ext;
		struct tex_shader tex_effects_rgba;
		struct tex_shader tex_effects_rgbx;
		struct tex_shader tex_effects_ext;

		struct box_shadow_shader box_shadow;
		struct blur_shader blur1;
		struct blur_shader blur2;
		struct blur_effects_shader blur_effects;
	} shaders;

	TRACY_FN(
		struct tracy_data *tracy_data;
	)

	struct wl_list buffers; // fx_framebuffer.link
	struct wl_list offscreen_buffers; // fx_offscreen_buffers.link

	struct wl_listener renderer_destroy;
};

#endif
