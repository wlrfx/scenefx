#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include "render/gles2/egl.h"
#include "render/gles2/gles2.h"
#include "render/fx_renderer.h"

static inline void free_shaders(struct gles2_renderer *gles2_renderer) {
	push_fx_debug(gles2_renderer);
	glDeleteProgram(gles2_renderer->shaders.quad.program);
	glDeleteProgram(gles2_renderer->shaders.quad_clip.program);
	glDeleteProgram(gles2_renderer->shaders.quad_round.program);
	glDeleteProgram(gles2_renderer->shaders.quad_grad.program);
	glDeleteProgram(gles2_renderer->shaders.quad_grad_round.program);
	glDeleteProgram(gles2_renderer->shaders.tex_rgba.program);
	glDeleteProgram(gles2_renderer->shaders.tex_rgbx.program);
	glDeleteProgram(gles2_renderer->shaders.tex_ext.program);
	glDeleteProgram(gles2_renderer->shaders.tex_effects_rgba.program);
	glDeleteProgram(gles2_renderer->shaders.tex_effects_rgbx.program);
	glDeleteProgram(gles2_renderer->shaders.tex_effects_ext.program);
	glDeleteProgram(gles2_renderer->shaders.box_shadow.program);
	glDeleteProgram(gles2_renderer->shaders.blur1.program);
	glDeleteProgram(gles2_renderer->shaders.blur2.program);
	glDeleteProgram(gles2_renderer->shaders.blur_effects.program);
	pop_fx_debug(gles2_renderer);
}

static bool link_shaders(struct gles2_renderer *gles2_renderer) {
	// quad fragment shader
	if (!link_quad_program(&gles2_renderer->shaders.quad, false)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}

	// quad clip fragment shader
	if (!link_quad_program(&gles2_renderer->shaders.quad_clip, true)) {
		wlr_log(WLR_ERROR, "Could not link quad clip shader");
		goto error;
	}

	// quad fragment shader with gradients
	if (!link_quad_grad_program(&gles2_renderer->shaders.quad_grad, 16)) {
		wlr_log(WLR_ERROR, "Could not link quad grad shader");
		goto error;
	}

	if (!link_quad_grad_round_program(&gles2_renderer->shaders.quad_grad_round, 16)) {
		wlr_log(WLR_ERROR, "Could not link quad grad round shader");
		goto error;
	}

	if (!link_quad_round_program(&gles2_renderer->shaders.quad_round)) {
		wlr_log(WLR_ERROR, "Could not link quad round shader");
		goto error;
	}

	// Basic fragment shaders
	if (!link_tex_program(&gles2_renderer->shaders.tex_rgba,
				SHADER_SOURCE_TEXTURE_RGBA, false)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&gles2_renderer->shaders.tex_rgbx,
				SHADER_SOURCE_TEXTURE_RGBX, false)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&gles2_renderer->shaders.tex_ext,
				SHADER_SOURCE_TEXTURE_EXTERNAL, false)) {
		wlr_log(WLR_ERROR, "Could not link tex_EXTERNAL shader");
		goto error;
	}

	// Effects fragment shaders
	if (!link_tex_program(&gles2_renderer->shaders.tex_effects_rgba,
				SHADER_SOURCE_TEXTURE_RGBA, true)) {
		wlr_log(WLR_ERROR, "Could not link tex_effects_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&gles2_renderer->shaders.tex_effects_rgbx,
				SHADER_SOURCE_TEXTURE_RGBX, true)) {
		wlr_log(WLR_ERROR, "Could not link tex_effects_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&gles2_renderer->shaders.tex_effects_ext,
				SHADER_SOURCE_TEXTURE_EXTERNAL, true)) {
		wlr_log(WLR_ERROR, "Could not link tex_effects_EXTERNAL shader");
		goto error;
	}

	// box shadow shader
	if (!link_box_shadow_program(&gles2_renderer->shaders.box_shadow)) {
		wlr_log(WLR_ERROR, "Could not link box shadow shader");
		goto error;
	}

	// Blur shaders
	if (!link_blur1_program(&gles2_renderer->shaders.blur1)) {
		wlr_log(WLR_ERROR, "Could not link blur1 shader");
		goto error;
	}
	if (!link_blur2_program(&gles2_renderer->shaders.blur2)) {
		wlr_log(WLR_ERROR, "Could not link blur2 shader");
		goto error;
	}
	if (!link_blur_effects_program(&gles2_renderer->shaders.blur_effects)) {
		wlr_log(WLR_ERROR, "Could not link blur_effects shader");
		goto error;
	}

	return true;

error:
	free_shaders(gles2_renderer);
	return false;
}

static void offscreen_buffers_destroy(struct fx_offscreen_buffers *fx_offscreen_buffers) {
	struct gles2_offscreen_buffers *gles2_offscreen_buffers = gles2_get_offscreen_buffers(fx_offscreen_buffers);
	// Make sure to free the buffers
	if (gles2_offscreen_buffers->optimized_blur_buffer != NULL) {
		wlr_buffer_drop(gles2_offscreen_buffers->optimized_blur_buffer->wlr_buffer);
		gles2_offscreen_buffers->optimized_blur_buffer = NULL;
	}
	if (gles2_offscreen_buffers->optimized_no_blur_buffer != NULL) {
		wlr_buffer_drop(gles2_offscreen_buffers->optimized_no_blur_buffer->wlr_buffer);
		gles2_offscreen_buffers->optimized_no_blur_buffer = NULL;
	}
	if (gles2_offscreen_buffers->blur_saved_pixels_buffer != NULL) {
		wlr_buffer_drop(gles2_offscreen_buffers->blur_saved_pixels_buffer->wlr_buffer);
		gles2_offscreen_buffers->blur_saved_pixels_buffer = NULL;
	}
	if (gles2_offscreen_buffers->effects_buffer != NULL) {
		wlr_buffer_drop(gles2_offscreen_buffers->effects_buffer->wlr_buffer);
		gles2_offscreen_buffers->effects_buffer = NULL;
	}
	if (gles2_offscreen_buffers->effects_buffer_swapped != NULL) {
		wlr_buffer_drop(gles2_offscreen_buffers->effects_buffer_swapped->wlr_buffer);
		gles2_offscreen_buffers->effects_buffer_swapped = NULL;
	}

	free(gles2_offscreen_buffers);
}

static const struct fx_offscreen_buffers_impl offscreen_buffers_impl = {
	.destroy = offscreen_buffers_destroy,
};

static struct fx_offscreen_buffers *offscreen_buffers_allocate(
		struct fx_renderer *fx_renderer, struct wlr_output *output) {
	struct gles2_offscreen_buffers *gles2_offscreen_buffers = calloc(1, sizeof(*gles2_offscreen_buffers));
	if (gles2_offscreen_buffers == NULL) {
		wlr_log_errno(WLR_ERROR, "gles2_offscreen_buffers allocation failed");
		return NULL;
	}

	fx_offscreen_buffers_init(&gles2_offscreen_buffers->fx_offscreen_buffers,
			&offscreen_buffers_impl, fx_renderer, output);
	return &gles2_offscreen_buffers->fx_offscreen_buffers;
}

static void renderer_destroy(struct fx_renderer *fx_renderer) {
	struct gles2_renderer *gles2_renderer = gles2_get_renderer(fx_renderer);
	egl_ensure_current(gles2_renderer);

	struct gles2_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &gles2_renderer->buffers, link) {
		gles2_buffer_destroy(buffer);
	}

	free_shaders(gles2_renderer);

	gles2_renderer->wlr_egl = NULL;

	free(gles2_renderer);
}

static const struct fx_renderer_impl renderer_impl = {
	// .name = "gles2_renderer",
	.offscreen_buffers_allocate = offscreen_buffers_allocate,
	.render_pass_allocate = gles2_render_pass_init,
	.renderer_destroy = renderer_destroy,

#ifdef TRACY_ENABLE
	.tracy_gpu_zone_begin = gles2_tracy_gpu_zone_begin,
	.tracy_gpu_zone_end = gles2_tracy_gpu_zone_end,
	.tracy_gpu_context_collect = gles2_tracy_gpu_context_collect,
	.tracy_gpu_context_destroy = gles2_tracy_gpu_context_destroy,
	.tracy_gpu_context_new = gles2_tracy_gpu_context_new,
#endif
};

struct fx_renderer *gles2_renderer_create(struct wlr_renderer *wlr_renderer) {
	struct gles2_renderer *gles2_renderer = calloc(1, sizeof(*gles2_renderer));
	if (gles2_renderer == NULL) {
		return NULL;
	}

	wl_list_init(&gles2_renderer->buffers);

	gles2_renderer->wlr_egl = wlr_gles2_renderer_get_egl(wlr_renderer);
	assert(gles2_renderer->wlr_egl);
	if (!egl_ensure_current(gles2_renderer)) {
		goto error;
	}

	// EGL extensions
	const char *egl_exts_str = eglQueryString(wlr_egl_get_display(gles2_renderer->wlr_egl), EGL_EXTENSIONS);
	if (egl_exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to query EGL display extensions");
		goto gl_error;
	}

	if (check_egl_ext(egl_exts_str, "EGL_KHR_fence_sync") &&
			check_egl_ext(egl_exts_str, "EGL_ANDROID_native_fence_sync")) {
		load_egl_proc(&gles2_renderer->egl_procs.eglCreateSyncKHR, "eglCreateSyncKHR");
		load_egl_proc(&gles2_renderer->egl_procs.eglDestroySyncKHR, "eglDestroySyncKHR");
	}

	if (check_egl_ext(egl_exts_str, "EGL_KHR_wait_sync")) {
		load_egl_proc(&gles2_renderer->egl_procs.eglWaitSyncKHR, "eglWaitSyncKHR");
	}

	// OpenGL extensions
	const char *gl_exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (gl_exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to get GL_EXTENSIONS");
		goto gl_error;
	}

	if (check_gl_ext(gl_exts_str, "GL_OES_EGL_image_external")) {
		gles2_renderer->exts.OES_egl_image_external = true;
	}

	if (check_gl_ext(gl_exts_str, "GL_EXT_disjoint_timer_query")) {
		gles2_renderer->exts.EXT_disjoint_timer_query = true;
		load_gl_proc(&gles2_renderer->gl_procs.glGenQueriesEXT, "glGenQueriesEXT");
		load_gl_proc(&gles2_renderer->gl_procs.glDeleteQueriesEXT, "glDeleteQueriesEXT");
		load_gl_proc(&gles2_renderer->gl_procs.glQueryCounterEXT, "glQueryCounterEXT");
		load_gl_proc(&gles2_renderer->gl_procs.glGetQueryObjectivEXT, "glGetQueryObjectivEXT");
		load_gl_proc(&gles2_renderer->gl_procs.glGetQueryObjectui64vEXT, "glGetQueryObjectui64vEXT");
		if (eglGetProcAddress("glGetInteger64vEXT")) {
			load_gl_proc(&gles2_renderer->gl_procs.glGetInteger64vEXT, "glGetInteger64vEXT");
		} else {
			load_gl_proc(&gles2_renderer->gl_procs.glGetInteger64vEXT, "glGetInteger64v");
		}
		TRACY_FN(
			load_gl_proc(&gles2_renderer->gl_procs.glGetQueryivEXT, "glGetQueryivEXT");
		)
	}

	push_fx_debug(gles2_renderer);

	// Link all shaders
	if (!link_shaders(gles2_renderer)) {
		goto gl_error;
	}

	pop_fx_debug(gles2_renderer);

	fx_renderer_init(&gles2_renderer->fx_renderer, &renderer_impl, wlr_renderer);

	wlr_log(WLR_INFO, "GLES2 FX RENDERER: Shaders Initialized Successfully");

	return &gles2_renderer->fx_renderer;
gl_error:
	pop_fx_debug(gles2_renderer);
error:
	free(gles2_renderer);
	return NULL;
}

void gles2_framebuffer_bind(struct gles2_buffer *gles2_buffer) {
	glBindFramebuffer(GL_FRAMEBUFFER, gles2_buffer->fbo);
}

static bool fx_renderer_is_gles2(const struct fx_renderer *fx_renderer) {
	return fx_renderer->impl == &renderer_impl;
}

static bool fx_offscreen_buffers_is_gles2(const struct fx_offscreen_buffers *offscreen_buffers) {
	return offscreen_buffers->impl == &offscreen_buffers_impl;
}

struct gles2_renderer *gles2_get_renderer(struct fx_renderer *fx_renderer) {
	assert(fx_renderer_is_gles2(fx_renderer));
	struct gles2_renderer *renderer = wl_container_of(fx_renderer, renderer, fx_renderer);
	return renderer;
}

struct gles2_offscreen_buffers *gles2_get_offscreen_buffers(
		struct fx_offscreen_buffers *offscreen_buffers) {
	assert(fx_renderer_is_gles2(offscreen_buffers->fx_renderer));
	assert(fx_offscreen_buffers_is_gles2(offscreen_buffers));
	struct gles2_offscreen_buffers *offscreen
		= wl_container_of(offscreen_buffers, offscreen, fx_offscreen_buffers);
	return offscreen;
}

struct gles2_render_pass *gles2_get_render_pass(struct fx_render_pass *fx_render_pass) {
	assert(fx_renderer_is_gles2(fx_render_pass->fx_renderer));
	struct gles2_render_pass *pass = wl_container_of(fx_render_pass, pass, fx_render_pass);
	return pass;
}

void push_fx_debug_(struct gles2_renderer *gles2_renderer,
		const char *file, const char *func) {
	if (!gles2_renderer->gl_procs.glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	gles2_renderer->gl_procs.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_fx_debug(struct gles2_renderer *gles2_renderer) {
	if (gles2_renderer->gl_procs.glPopDebugGroupKHR) {
		gles2_renderer->gl_procs.glPopDebugGroupKHR();
	}
}

