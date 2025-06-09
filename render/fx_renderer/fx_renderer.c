/*
	The original wlr_renderer was heavily referenced in making this project
	https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/render/gles2
*/

#include <assert.h>
#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <xf86drm.h>

#include "render/egl.h"
#include "render/fx_renderer/shaders.h"
#include "render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/util.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"
#include "scenefx/render/pass.h"
#include "util/time.h"

static const struct wlr_renderer_impl renderer_impl;
static const struct wlr_render_timer_impl render_timer_impl;

bool wlr_renderer_is_fx(struct wlr_renderer *wlr_renderer) {
	return wlr_renderer->impl == &renderer_impl;
}

struct fx_renderer *fx_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer_is_fx(wlr_renderer));
	struct fx_renderer *renderer = wl_container_of(wlr_renderer, renderer, wlr_renderer);
	return renderer;
}

bool wlr_render_timer_is_fx(struct wlr_render_timer *timer) {
	return timer->impl == &render_timer_impl;
}

struct fx_render_timer *fx_get_render_timer(struct wlr_render_timer *wlr_timer) {
	assert(wlr_render_timer_is_fx(wlr_timer));
	struct fx_render_timer *timer = wl_container_of(wlr_timer, timer, base);
	return timer;
}

static const struct wlr_drm_format_set *fx_get_texture_formats(
		struct wlr_renderer *wlr_renderer, uint32_t buffer_caps) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	if (buffer_caps & WLR_BUFFER_CAP_DMABUF) {
		return wlr_egl_get_dmabuf_texture_formats(renderer->egl);
	} else if (buffer_caps & WLR_BUFFER_CAP_DATA_PTR) {
		return &renderer->shm_texture_formats;
	} else {
		return NULL;
	}
}

static const struct wlr_drm_format_set *fx_get_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	return wlr_egl_get_dmabuf_render_formats(renderer->egl);
}

static int fx_get_drm_fd(struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer =
		fx_get_renderer(wlr_renderer);

	if (renderer->drm_fd < 0) {
		renderer->drm_fd = wlr_egl_dup_drm_fd(renderer->egl);
	}

	return renderer->drm_fd;
}

static void fx_renderer_destroy(struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);

	wlr_egl_make_current(renderer->egl, NULL);

	struct fx_texture *tex, *tex_tmp;
	wl_list_for_each_safe(tex, tex_tmp, &renderer->textures, link) {
		fx_texture_destroy(tex);
	}

	struct fx_framebuffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
		fx_framebuffer_destroy(buffer);
	}

	push_fx_debug(renderer);
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);
	pop_fx_debug(renderer);

	if (renderer->exts.KHR_debug) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		renderer->procs.glDebugMessageCallbackKHR(NULL, NULL);
	}

	wlr_egl_unset_current(renderer->egl);
	wlr_egl_destroy(renderer->egl);

	wlr_drm_format_set_finish(&renderer->shm_texture_formats);

	if (renderer->drm_fd >= 0) {
		close(renderer->drm_fd);
	}

	free(renderer);
}

static struct wlr_render_pass *begin_buffer_pass(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer, const struct wlr_buffer_pass_options *options) {
	struct fx_gles_render_pass *pass =
		fx_renderer_begin_buffer_pass(wlr_renderer, wlr_buffer, NULL, &(struct fx_buffer_pass_options) {
					.base = options,
					.swapchain = NULL,
				});
	if (!pass) {
		return NULL;
	}
	return &pass->base;
}

GLuint fx_renderer_get_buffer_fbo(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	GLuint fbo = 0;

	struct wlr_egl_context prev_ctx = {0};
	if (!wlr_egl_make_current(renderer->egl, &prev_ctx)) {
		return 0;
	}

	struct fx_framebuffer *buffer = fx_framebuffer_get_or_create(renderer, wlr_buffer);
	if (buffer) {
		fbo = fx_framebuffer_get_fbo(buffer);
	}

	wlr_egl_restore_context(&prev_ctx);
	return fbo;
}


static struct wlr_render_timer *fx_render_timer_create(struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	if (!renderer->exts.EXT_disjoint_timer_query) {
		wlr_log(WLR_ERROR, "can't create timer, EXT_disjoint_timer_query not available");
		return NULL;
	}

	struct fx_render_timer *timer = calloc(1, sizeof(*timer));
	if (!timer) {
		return NULL;
	}
	timer->base.impl = &render_timer_impl;
	timer->renderer = renderer;

	struct wlr_egl_context prev_ctx;
	wlr_egl_make_current(renderer->egl, &prev_ctx);
	renderer->procs.glGenQueriesEXT(1, &timer->id);
	wlr_egl_restore_context(&prev_ctx);

	return &timer->base;
}

static int fx_get_render_time(struct wlr_render_timer *wlr_timer) {
	struct fx_render_timer *timer = fx_get_render_timer(wlr_timer);
	struct fx_renderer *renderer = timer->renderer;

	struct wlr_egl_context prev_ctx;
	wlr_egl_make_current(renderer->egl, &prev_ctx);

	GLint64 disjoint;
	renderer->procs.glGetInteger64vEXT(GL_GPU_DISJOINT_EXT, &disjoint);
	if (disjoint) {
		wlr_log(WLR_ERROR, "a disjoint operation occurred and the render timer is invalid");
		wlr_egl_restore_context(&prev_ctx);
		return -1;
	}

	GLint available;
	renderer->procs.glGetQueryObjectivEXT(timer->id,
		GL_QUERY_RESULT_AVAILABLE_EXT, &available);
	if (!available) {
		wlr_log(WLR_ERROR, "timer was read too early, gpu isn't done!");
		wlr_egl_restore_context(&prev_ctx);
		return -1;
	}

	GLuint64 gl_render_end;
	renderer->procs.glGetQueryObjectui64vEXT(timer->id, GL_QUERY_RESULT_EXT,
		&gl_render_end);

	int64_t cpu_nsec_total = timespec_to_nsec(&timer->cpu_end) - timespec_to_nsec(&timer->cpu_start);

	wlr_egl_restore_context(&prev_ctx);
	return gl_render_end - timer->gl_cpu_end + cpu_nsec_total;
}

static void fx_render_timer_destroy(struct wlr_render_timer *wlr_timer) {
	struct fx_render_timer *timer = wl_container_of(wlr_timer, timer, base);
	struct fx_renderer *renderer = timer->renderer;

	struct wlr_egl_context prev_ctx;
	wlr_egl_make_current(renderer->egl, &prev_ctx);
	renderer->procs.glDeleteQueriesEXT(1, &timer->id);
	wlr_egl_restore_context(&prev_ctx);
	free(timer);
}

static const struct wlr_renderer_impl renderer_impl = {
	.destroy = fx_renderer_destroy,
	.get_texture_formats = fx_get_texture_formats,
	.get_render_formats = fx_get_render_formats,
	.get_drm_fd = fx_get_drm_fd,
	.texture_from_buffer = fx_texture_from_buffer,
	.begin_buffer_pass = begin_buffer_pass,
	.render_timer_create = fx_render_timer_create,
};

static const struct wlr_render_timer_impl render_timer_impl = {
	.get_duration_ns = fx_get_render_time,
	.destroy = fx_render_timer_destroy,
};

void push_fx_debug_(struct fx_renderer *renderer,
		const char *file, const char *func) {
	if (!renderer->procs.glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	renderer->procs.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_fx_debug(struct fx_renderer *renderer) {
	if (renderer->procs.glPopDebugGroupKHR) {
		renderer->procs.glPopDebugGroupKHR();
	}
}

static enum wlr_log_importance fx_log_importance_to_wlr(GLenum type) {
	switch (type) {
	case GL_DEBUG_TYPE_ERROR_KHR:               return WLR_ERROR;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR: return WLR_DEBUG;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:  return WLR_ERROR;
	case GL_DEBUG_TYPE_PORTABILITY_KHR:         return WLR_DEBUG;
	case GL_DEBUG_TYPE_PERFORMANCE_KHR:         return WLR_DEBUG;
	case GL_DEBUG_TYPE_OTHER_KHR:               return WLR_DEBUG;
	case GL_DEBUG_TYPE_MARKER_KHR:              return WLR_DEBUG;
	case GL_DEBUG_TYPE_PUSH_GROUP_KHR:          return WLR_DEBUG;
	case GL_DEBUG_TYPE_POP_GROUP_KHR:           return WLR_DEBUG;
	default:                                    return WLR_DEBUG;
	}
}

static void fx_log(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const GLchar *msg, const void *user) {
	_wlr_log(fx_log_importance_to_wlr(type), "[GLES2] %s", msg);
}

static struct wlr_renderer *renderer_autocreate(struct wlr_backend *backend, int drm_fd) {
	bool own_drm_fd = false;
	if (!open_preferred_drm_fd(backend, &drm_fd, &own_drm_fd)) {
		wlr_log(WLR_ERROR, "Cannot create GLES2 renderer: no DRM FD available");
		return NULL;
	}

	struct wlr_egl *egl = wlr_egl_create_with_drm_fd(drm_fd);
	if (egl == NULL) {
		wlr_log(WLR_ERROR, "Could not initialize EGL");
		return NULL;
	}

	struct wlr_renderer *renderer = fx_renderer_create_egl(egl);
	if (!renderer) {
		wlr_log(WLR_ERROR, "Failed to create the FX renderer");
		wlr_egl_destroy(egl);
		return NULL;
	}

	if (own_drm_fd && drm_fd >= 0) {
		close(drm_fd);
	}

	return renderer;
}

struct wlr_renderer *fx_renderer_create_with_drm_fd(int drm_fd) {
	assert(drm_fd >= 0);

	return renderer_autocreate(NULL, drm_fd);
}

struct wlr_renderer *fx_renderer_create(struct wlr_backend *backend) {
	return renderer_autocreate(backend, -1);
}

static bool link_shaders(struct fx_renderer *renderer) {
	EGLint client_version;
	eglQueryContext(renderer->egl->display, renderer->egl->context,
		EGL_CONTEXT_CLIENT_VERSION, &client_version);

	// quad fragment shader
	if (!link_quad_program(&renderer->shaders.quad, (GLint) client_version)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}

	// quad fragment shader with gradients
	if (!link_quad_grad_program(&renderer->shaders.quad_grad, (GLint) client_version, 16)) {
		wlr_log(WLR_ERROR, "Could not link quad grad shader");
		goto error;
	}

	if (!link_quad_grad_round_program(&renderer->shaders.quad_grad_round, (GLint) client_version, 16)) {
		wlr_log(WLR_ERROR, "Could not link quad grad round shader");
		goto error;
	}

	if (!link_quad_round_program(&renderer->shaders.quad_round, (GLint) client_version)) {
		wlr_log(WLR_ERROR, "Could not link quad round shader");
		goto error;
	}

	// fragment shaders
	if (!link_tex_program(&renderer->shaders.tex_rgba, (GLint) client_version, SHADER_SOURCE_TEXTURE_RGBA)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_rgbx, (GLint) client_version, SHADER_SOURCE_TEXTURE_RGBX)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_ext, (GLint) client_version, SHADER_SOURCE_TEXTURE_EXTERNAL)) {
		wlr_log(WLR_ERROR, "Could not link tex_EXTERNAL shader");
		goto error;
	}

	// box shadow shader
	if (!link_box_shadow_program(&renderer->shaders.box_shadow, (GLint) client_version)) {
		wlr_log(WLR_ERROR, "Could not link box shadow shader");
		goto error;
	}

	// Blur shaders
	if (!link_blur1_program(&renderer->shaders.blur1, (GLint) client_version)) {
		wlr_log(WLR_ERROR, "Could not link blur1 shader");
		goto error;
	}
	if (!link_blur2_program(&renderer->shaders.blur2, (GLint) client_version)) {
		wlr_log(WLR_ERROR, "Could not link blur2 shader");
		goto error;
	}
	if (!link_blur_effects_program(&renderer->shaders.blur_effects, (GLint) client_version)) {
		wlr_log(WLR_ERROR, "Could not link blur_effects shader");
		goto error;
	}

	return true;

error:
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.quad_round.program);
	glDeleteProgram(renderer->shaders.quad_grad.program);
	glDeleteProgram(renderer->shaders.quad_grad_round.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);
	glDeleteProgram(renderer->shaders.box_shadow.program);
	glDeleteProgram(renderer->shaders.blur1.program);
	glDeleteProgram(renderer->shaders.blur2.program);
	glDeleteProgram(renderer->shaders.blur_effects.program);

	return false;
}

struct wlr_renderer *fx_renderer_create_egl(struct wlr_egl *egl) {
	if (!wlr_egl_make_current(egl, NULL)) {
		return NULL;
	}

	const char *exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to get GL_EXTENSIONS");
		return NULL;
	}

	struct fx_renderer *renderer = calloc(1, sizeof(struct fx_renderer));
	if (renderer == NULL) {
		return NULL;
	}
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl, WLR_BUFFER_CAP_DMABUF);
	renderer->wlr_renderer.features.output_color_transform = false;

	wl_list_init(&renderer->buffers);
	wl_list_init(&renderer->textures);

	renderer->egl = egl;
	renderer->exts_str = exts_str;
	renderer->drm_fd = -1;

	wlr_log(WLR_INFO, "Creating scenefx FX renderer");
	wlr_log(WLR_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(WLR_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
	wlr_log(WLR_INFO, "GL renderer: %s", glGetString(GL_RENDERER));
	wlr_log(WLR_INFO, "Supported FX extensions: %s", exts_str);

	if (!renderer->egl->exts.EXT_image_dma_buf_import) {
		wlr_log(WLR_ERROR, "EGL_EXT_image_dma_buf_import not supported");
		free(renderer);
		return NULL;
	}
	if (!check_gl_ext(exts_str, "GL_EXT_texture_format_BGRA8888")) {
		wlr_log(WLR_ERROR, "BGRA8888 format not supported by GLES2");
		free(renderer);
		return NULL;
	}
	if (!check_gl_ext(exts_str, "GL_EXT_unpack_subimage")) {
		wlr_log(WLR_ERROR, "GL_EXT_unpack_subimage not supported");
		free(renderer);
		return NULL;
	}

	renderer->exts.EXT_read_format_bgra =
		check_gl_ext(exts_str, "GL_EXT_read_format_bgra");

	renderer->exts.EXT_texture_type_2_10_10_10_REV =
		check_gl_ext(exts_str, "GL_EXT_texture_type_2_10_10_10_REV");

	renderer->exts.OES_texture_half_float_linear =
		check_gl_ext(exts_str, "GL_OES_texture_half_float_linear");

	renderer->exts.EXT_texture_norm16 =
		check_gl_ext(exts_str, "GL_EXT_texture_norm16");

	if (check_gl_ext(exts_str, "GL_KHR_debug")) {
		renderer->exts.KHR_debug = true;
		load_gl_proc(&renderer->procs.glDebugMessageCallbackKHR,
			"glDebugMessageCallbackKHR");
		load_gl_proc(&renderer->procs.glDebugMessageControlKHR,
			"glDebugMessageControlKHR");
	}

	// TODO: the rest of the gl checks
	if (check_gl_ext(exts_str, "GL_OES_EGL_image_external")) {
		renderer->exts.OES_egl_image_external = true;
		load_gl_proc(&renderer->procs.glEGLImageTargetTexture2DOES,
			"glEGLImageTargetTexture2DOES");
	}

	if (check_gl_ext(exts_str, "GL_OES_EGL_image")) {
		renderer->exts.OES_egl_image = true;
		load_gl_proc(&renderer->procs.glEGLImageTargetRenderbufferStorageOES,
			"glEGLImageTargetRenderbufferStorageOES");
	}

	if (check_gl_ext(exts_str, "GL_KHR_robustness")) {
		GLint notif_strategy = 0;
		glGetIntegerv(GL_RESET_NOTIFICATION_STRATEGY_KHR, &notif_strategy);
		switch (notif_strategy) {
		case GL_LOSE_CONTEXT_ON_RESET_KHR:
			wlr_log(WLR_DEBUG, "GPU reset notifications are enabled");
			load_gl_proc(&renderer->procs.glGetGraphicsResetStatusKHR,
				"glGetGraphicsResetStatusKHR");
			break;
		case GL_NO_RESET_NOTIFICATION_KHR:
			wlr_log(WLR_DEBUG, "GPU reset notifications are disabled");
			break;
		}
	}

	if (check_gl_ext(exts_str, "GL_EXT_disjoint_timer_query")) {
		renderer->exts.EXT_disjoint_timer_query = true;
		load_gl_proc(&renderer->procs.glGenQueriesEXT, "glGenQueriesEXT");
		load_gl_proc(&renderer->procs.glDeleteQueriesEXT, "glDeleteQueriesEXT");
		load_gl_proc(&renderer->procs.glQueryCounterEXT, "glQueryCounterEXT");
		load_gl_proc(&renderer->procs.glGetQueryObjectivEXT, "glGetQueryObjectivEXT");
		load_gl_proc(&renderer->procs.glGetQueryObjectui64vEXT, "glGetQueryObjectui64vEXT");
		if (eglGetProcAddress("glGetInteger64vEXT")) {
			load_gl_proc(&renderer->procs.glGetInteger64vEXT, "glGetInteger64vEXT");
		} else {
			load_gl_proc(&renderer->procs.glGetInteger64vEXT, "glGetInteger64v");
		}
	}

	if (renderer->exts.KHR_debug) {
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		renderer->procs.glDebugMessageCallbackKHR(fx_log, NULL);

		// Silence unwanted message types
		renderer->procs.glDebugMessageControlKHR(GL_DONT_CARE,
			GL_DEBUG_TYPE_POP_GROUP_KHR, GL_DONT_CARE, 0, NULL, GL_FALSE);
		renderer->procs.glDebugMessageControlKHR(GL_DONT_CARE,
			GL_DEBUG_TYPE_PUSH_GROUP_KHR, GL_DONT_CARE, 0, NULL, GL_FALSE);
	}

	push_fx_debug(renderer);

	// Link all shaders
	if (!link_shaders(renderer)) {
		goto error;
	}

	pop_fx_debug(renderer);

	wlr_log(WLR_INFO, "FX RENDERER: Shaders Initialized Successfully");

	wlr_egl_unset_current(renderer->egl);

	get_fx_shm_formats(renderer, &renderer->shm_texture_formats);

	int drm_fd = wlr_renderer_get_drm_fd(&renderer->wlr_renderer);
	uint64_t cap_syncobj_timeline;
	if (drm_fd >= 0 && drmGetCap(drm_fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap_syncobj_timeline) == 0) {
		renderer->wlr_renderer.features.timeline = egl->procs.eglDupNativeFenceFDANDROID &&
			egl->procs.eglWaitSyncKHR && cap_syncobj_timeline != 0;
	}

	return &renderer->wlr_renderer;

error:
	pop_fx_debug(renderer);

	if (renderer->exts.KHR_debug) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		renderer->procs.glDebugMessageCallbackKHR(NULL, NULL);
	}

	wlr_egl_unset_current(renderer->egl);

	free(renderer);
	return NULL;
}
