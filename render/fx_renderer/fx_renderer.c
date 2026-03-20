#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include "render/egl.h"
#include "render/fx_renderer/fx_renderer.h"
#include "render/tracy.h"
#include "scenefx/render/fx_renderer/fx_offscreen_buffers.h"
#include "scenefx/scenefx.h"

static inline void free_shaders(struct fx_renderer *fx_renderer) {
	push_fx_debug(fx_renderer);
	glDeleteProgram(fx_renderer->shaders.quad.program);
	glDeleteProgram(fx_renderer->shaders.quad_clip.program);
	glDeleteProgram(fx_renderer->shaders.quad_round.program);
	glDeleteProgram(fx_renderer->shaders.quad_grad.program);
	glDeleteProgram(fx_renderer->shaders.quad_grad_round.program);
	glDeleteProgram(fx_renderer->shaders.tex_rgba.program);
	glDeleteProgram(fx_renderer->shaders.tex_rgbx.program);
	glDeleteProgram(fx_renderer->shaders.tex_ext.program);
	glDeleteProgram(fx_renderer->shaders.tex_effects_rgba.program);
	glDeleteProgram(fx_renderer->shaders.tex_effects_rgbx.program);
	glDeleteProgram(fx_renderer->shaders.tex_effects_ext.program);
	glDeleteProgram(fx_renderer->shaders.box_shadow.program);
	glDeleteProgram(fx_renderer->shaders.blur1.program);
	glDeleteProgram(fx_renderer->shaders.blur2.program);
	glDeleteProgram(fx_renderer->shaders.blur_effects.program);
	pop_fx_debug(fx_renderer);
}

static void scenefx_destroy(struct fx_renderer *fx_renderer) {
	TRACY_GPU_CONTEXT_DESTROY(fx_renderer->tracy_data);

	wl_list_remove(&fx_renderer->renderer_destroy.link);

	egl_ensure_current(fx_renderer);

	struct fx_offscreen_buffers *fbos, *fbos_tmp;
	wl_list_for_each_safe(fbos, fbos_tmp, &fx_renderer->offscreen_buffers, link) {
		fx_offscreen_buffers_destroy(fbos);
	}

	struct fx_framebuffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &fx_renderer->buffers, link) {
		fx_framebuffer_destroy(buffer);
	}

	free_shaders(fx_renderer);

	fx_renderer->wlr_egl = NULL;
	fx_renderer->wlr_renderer = NULL;

	free(fx_renderer);
}

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_renderer *fx_renderer = wl_container_of(listener, fx_renderer, renderer_destroy);

	wlr_log(WLR_INFO, "wlr_renderer being destroyed, destroying fx_renderer");
	scenefx_destroy(fx_renderer);
}

void push_fx_debug_(struct fx_renderer *fx_renderer,
		const char *file, const char *func) {
	if (!fx_renderer->gl_procs.glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	fx_renderer->gl_procs.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_fx_debug(struct fx_renderer *fx_renderer) {
	if (fx_renderer->gl_procs.glPopDebugGroupKHR) {
		fx_renderer->gl_procs.glPopDebugGroupKHR();
	}
}

static bool link_shaders(struct fx_renderer *fx_renderer) {
	// quad fragment shader
	if (!link_quad_program(&fx_renderer->shaders.quad, false)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}

	// quad clip fragment shader
	if (!link_quad_program(&fx_renderer->shaders.quad_clip, true)) {
		wlr_log(WLR_ERROR, "Could not link quad clip shader");
		goto error;
	}

	// quad fragment shader with gradients
	if (!link_quad_grad_program(&fx_renderer->shaders.quad_grad, 16)) {
		wlr_log(WLR_ERROR, "Could not link quad grad shader");
		goto error;
	}

	if (!link_quad_grad_round_program(&fx_renderer->shaders.quad_grad_round, 16)) {
		wlr_log(WLR_ERROR, "Could not link quad grad round shader");
		goto error;
	}

	if (!link_quad_round_program(&fx_renderer->shaders.quad_round)) {
		wlr_log(WLR_ERROR, "Could not link quad round shader");
		goto error;
	}

	// Basic fragment shaders
	if (!link_tex_program(&fx_renderer->shaders.tex_rgba,
				SHADER_SOURCE_TEXTURE_RGBA, false)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&fx_renderer->shaders.tex_rgbx,
				SHADER_SOURCE_TEXTURE_RGBX, false)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&fx_renderer->shaders.tex_ext,
				SHADER_SOURCE_TEXTURE_EXTERNAL, false)) {
		wlr_log(WLR_ERROR, "Could not link tex_EXTERNAL shader");
		goto error;
	}

	// Effects fragment shaders
	if (!link_tex_program(&fx_renderer->shaders.tex_effects_rgba,
				SHADER_SOURCE_TEXTURE_RGBA, true)) {
		wlr_log(WLR_ERROR, "Could not link tex_effects_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&fx_renderer->shaders.tex_effects_rgbx,
				SHADER_SOURCE_TEXTURE_RGBX, true)) {
		wlr_log(WLR_ERROR, "Could not link tex_effects_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&fx_renderer->shaders.tex_effects_ext,
				SHADER_SOURCE_TEXTURE_EXTERNAL, true)) {
		wlr_log(WLR_ERROR, "Could not link tex_effects_EXTERNAL shader");
		goto error;
	}

	// box shadow shader
	if (!link_box_shadow_program(&fx_renderer->shaders.box_shadow)) {
		wlr_log(WLR_ERROR, "Could not link box shadow shader");
		goto error;
	}

	// Blur shaders
	if (!link_blur1_program(&fx_renderer->shaders.blur1)) {
		wlr_log(WLR_ERROR, "Could not link blur1 shader");
		goto error;
	}
	if (!link_blur2_program(&fx_renderer->shaders.blur2)) {
		wlr_log(WLR_ERROR, "Could not link blur2 shader");
		goto error;
	}
	if (!link_blur_effects_program(&fx_renderer->shaders.blur_effects)) {
		wlr_log(WLR_ERROR, "Could not link blur_effects shader");
		goto error;
	}

	return true;

error:
	free_shaders(fx_renderer);
	return false;
}

struct wlr_renderer *scenefx_init(struct wlr_scene *wlr_scene, struct wlr_backend *backend) {
	assert(wlr_scene);
	struct wlr_renderer *wlr_renderer = wlr_renderer_autocreate(backend);
	if (!wlr_renderer) {
		wlr_log(WLR_ERROR, "Could not create wlr_renderer");
		return NULL;
	}

	if (!wlr_renderer_is_gles2(wlr_renderer)) {
		wlr_log(WLR_ERROR, "Could not initialize SceneFX. WLR_RENDERER must be gles2");
		return NULL;
	}

	struct fx_renderer *fx_renderer = calloc(1, sizeof(struct fx_renderer));
	if (fx_renderer == NULL) {
		return NULL;
	}

	fx_renderer->wlr_renderer = wlr_renderer;
	wlr_scene->fx_renderer = fx_renderer;

	wl_list_init(&fx_renderer->buffers);
	wl_list_init(&fx_renderer->offscreen_buffers);

	fx_renderer->wlr_egl = wlr_gles2_renderer_get_egl(wlr_renderer);
	assert(fx_renderer->wlr_egl);
	if (!egl_ensure_current(fx_renderer)) {
		goto error;
	}

	// EGL extensions
	const char *egl_exts_str = eglQueryString(wlr_egl_get_display(fx_renderer->wlr_egl), EGL_EXTENSIONS);
	if (egl_exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to query EGL display extensions");
		goto gl_error;
	}

	if (check_egl_ext(egl_exts_str, "EGL_KHR_fence_sync") &&
			check_egl_ext(egl_exts_str, "EGL_ANDROID_native_fence_sync")) {
		load_egl_proc(&fx_renderer->egl_procs.eglCreateSyncKHR, "eglCreateSyncKHR");
		load_egl_proc(&fx_renderer->egl_procs.eglDestroySyncKHR, "eglDestroySyncKHR");
	}

	if (check_egl_ext(egl_exts_str, "EGL_KHR_wait_sync")) {
		load_egl_proc(&fx_renderer->egl_procs.eglWaitSyncKHR, "eglWaitSyncKHR");
	}

	// OpenGL extensions
	const char *gl_exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (gl_exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to get GL_EXTENSIONS");
		goto gl_error;
	}

	if (check_gl_ext(gl_exts_str, "GL_OES_EGL_image_external")) {
		fx_renderer->exts.OES_egl_image_external = true;
	}

	if (check_gl_ext(gl_exts_str, "GL_EXT_disjoint_timer_query")) {
		fx_renderer->exts.EXT_disjoint_timer_query = true;
		load_gl_proc(&fx_renderer->gl_procs.glGenQueriesEXT, "glGenQueriesEXT");
		load_gl_proc(&fx_renderer->gl_procs.glDeleteQueriesEXT, "glDeleteQueriesEXT");
		load_gl_proc(&fx_renderer->gl_procs.glQueryCounterEXT, "glQueryCounterEXT");
		load_gl_proc(&fx_renderer->gl_procs.glGetQueryObjectivEXT, "glGetQueryObjectivEXT");
		load_gl_proc(&fx_renderer->gl_procs.glGetQueryObjectui64vEXT, "glGetQueryObjectui64vEXT");
		if (eglGetProcAddress("glGetInteger64vEXT")) {
			load_gl_proc(&fx_renderer->gl_procs.glGetInteger64vEXT, "glGetInteger64vEXT");
		} else {
			load_gl_proc(&fx_renderer->gl_procs.glGetInteger64vEXT, "glGetInteger64v");
		}
		TRACY_FN(
			load_gl_proc(&fx_renderer->gl_procs.glGetQueryivEXT, "glGetQueryivEXT");
		)
	}

	push_fx_debug(fx_renderer);

	TRACY_FN(
		fx_renderer->tracy_data = TRACY_GPU_CONTEXT_NEW(fx_renderer);
	)

	// Link all shaders
	if (!link_shaders(fx_renderer)) {
		goto gl_error;
	}

	pop_fx_debug(fx_renderer);

	wlr_log(WLR_INFO, "FX RENDERER: Shaders Initialized Successfully");

	fx_renderer->renderer_destroy.notify = handle_renderer_destroy;
	wl_signal_add(&fx_renderer->wlr_renderer->events.destroy, &fx_renderer->renderer_destroy);

	return fx_renderer->wlr_renderer;

gl_error:
	pop_fx_debug(fx_renderer);
error:
	free(fx_renderer);
	return NULL;
}
