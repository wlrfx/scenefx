#ifndef _RENDERER_GLES2_GLES2_H
#define _RENDERER_GLES2_GLES2_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>

#include "render/fx_renderer.h"
#include "render/gles2/shaders.h"
#include "render/tracy.h"

struct gles2_renderer;

extern const struct fx_renderer_tracy_impl gles2_renderer_tracy_impl;

struct gles2_buffer {
	struct wlr_buffer *wlr_buffer;

	struct gles2_renderer *gles2_renderer;
	struct wl_list link; // gles2_renderer.buffers

	GLuint sb; // Stencil

	// fbo allocated by wlroots, don't destroy!
	GLuint fbo;

	struct wlr_addon addon;
};

struct gles2_buffer *gles2_buffer_get_or_create(struct fx_renderer *fx_renderer,
		struct wlr_buffer *wlr_buffer, bool needs_stencil);
void gles2_buffer_get_or_allocate(struct fx_renderer *fx_renderer,
		struct wlr_allocator *allocator, int width, int height, bool has_alpha,
		struct gles2_buffer **gles2_buffer, bool *failed);
void gles2_buffer_destroy(struct gles2_buffer *buffer);

struct gles2_offscreen_buffers {
	struct fx_offscreen_buffers fx_offscreen_buffers;

	// Contains the blurred background for tiled windows
	struct gles2_buffer *optimized_blur_buffer;
	// Contains the non-blurred background for tiled windows. Used for blurring
	// optimized surfaces with an alpha. Just as inefficient as the regular blur.
	struct gles2_buffer *optimized_no_blur_buffer;
	// Contains the original pixels to draw over the areas where artifact are visible
	struct gles2_buffer *blur_saved_pixels_buffer;
	// Blur swaps between the two effects buffers every time it scales the image
	// Buffer used for effects
	struct gles2_buffer *effects_buffer;
	// Swap buffer used for effects
	struct gles2_buffer *effects_buffer_swapped;
};

struct gles2_offscreen_buffers *gles2_get_offscreen_buffers(
		struct fx_offscreen_buffers *offscreen_buffers);

struct gles2_render_pass {
	struct fx_render_pass fx_render_pass;
	struct gles2_renderer *gles2_renderer;
	struct gles2_buffer *gles2_buffer;
	float projection_matrix[9];

	// Contains output-specific framebuffers.
	// NULL when no advanced effects like blur is being used in the current pass.
	// Call `fx_render_pass_init_offscreen_buffers` to use advanced effects.
	struct gles2_offscreen_buffers *gles2_offscreen_buffers;
};

struct gles2_render_pass *gles2_get_render_pass(struct fx_render_pass *fx_render_pass);

struct fx_render_pass *gles2_render_pass_init(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output);

struct gles2_renderer {
	struct fx_renderer fx_renderer;

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

	struct wl_list buffers; // gles2_buffer.link
};

struct fx_renderer *gles2_renderer_create(struct wlr_renderer *wlr_renderer);

struct gles2_renderer *gles2_get_renderer(struct fx_renderer *fx_renderer);

void push_fx_debug_(struct gles2_renderer *gles2_renderer, const char *file, const char *func);
#define push_fx_debug(renderer) push_fx_debug_(renderer, _WLR_FILENAME, __func__)
void pop_fx_debug(struct gles2_renderer *gles2_renderer);

#endif
