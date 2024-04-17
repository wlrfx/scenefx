/*
	The original wlr_renderer was heavily referenced in making this project
	https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/render/gles2
*/

#include "render/fx_renderer/shaders.h"
#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "render/egl.h"
#include "scenefx/render/pass.h"
#include "render/pixel_format.h"
#include "render/fx_renderer/util.h"
#include "render/fx_renderer/fx_renderer.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/matrix.h"
#include "util/time.h"

static const GLfloat verts[] = {
	1, 0, // top right
	0, 0, // top left
	1, 1, // bottom right
	0, 1, // bottom left
};

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

static struct fx_renderer *fx_get_renderer_in_context(
		struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	assert(wlr_egl_is_current(renderer->egl));
	assert(renderer->current_buffer != NULL);
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

static bool fx_bind_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);

	if (renderer->current_buffer != NULL) {
		assert(wlr_egl_is_current(renderer->egl));

		push_fx_debug(renderer);
		glFlush();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		pop_fx_debug(renderer);

		wlr_buffer_unlock(renderer->current_buffer->buffer);
		renderer->current_buffer = NULL;
	}

	if (wlr_buffer == NULL) {
		wlr_egl_unset_current(renderer->egl);
		return true;
	}

	wlr_egl_make_current(renderer->egl);

	struct fx_framebuffer *buffer = fx_framebuffer_get_or_create(renderer, wlr_buffer);
	if (buffer == NULL) {
		return false;
	}

	wlr_buffer_lock(wlr_buffer);
	renderer->current_buffer = buffer;

	push_fx_debug(renderer);
	glBindFramebuffer(GL_FRAMEBUFFER, renderer->current_buffer->fbo);
	pop_fx_debug(renderer);

	return true;
}

static const char *reset_status_str(GLenum status) {
	switch (status) {
	case GL_GUILTY_CONTEXT_RESET_KHR:
		return "guilty";
	case GL_INNOCENT_CONTEXT_RESET_KHR:
		return "innocent";
	case GL_UNKNOWN_CONTEXT_RESET_KHR:
		return "unknown";
	default:
		return "<invalid>";
	}
}

// TODO: Deprecate all older rendering functions?
static bool fx_renderer_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct fx_renderer *renderer =
		fx_get_renderer_in_context(wlr_renderer);

	push_fx_debug(renderer);

	if (renderer->procs.glGetGraphicsResetStatusKHR) {
		GLenum status = renderer->procs.glGetGraphicsResetStatusKHR();
		if (status != GL_NO_ERROR) {
			wlr_log(WLR_ERROR, "GPU reset (%s)", reset_status_str(status));
			wl_signal_emit_mutable(&wlr_renderer->events.lost, NULL);
			pop_fx_debug(renderer);
			return false;
		}
	}

	glViewport(0, 0, width, height);
	renderer->viewport_width = width;
	renderer->viewport_height = height;

	// refresh projection matrix
	matrix_projection(renderer->projection, width, height,
		WL_OUTPUT_TRANSFORM_FLIPPED_180);

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// XXX: maybe we should save output projection and remove some of the need
	// for users to sling matricies themselves

	pop_fx_debug(renderer);

	return true;
}

static void fx_renderer_end(struct wlr_renderer *wlr_renderer) {
	// no-op
}

static void fx_renderer_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	struct fx_renderer *renderer =
		fx_get_renderer_in_context(wlr_renderer);

	push_fx_debug(renderer);
	glClearColor(color[0], color[1], color[2], color[3]);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	pop_fx_debug(renderer);
}

static void fx_renderer_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	struct fx_renderer *renderer =
		fx_get_renderer_in_context(wlr_renderer);

	push_fx_debug(renderer);
	if (box != NULL) {
		glScissor(box->x, box->y, box->width, box->height);
		glEnable(GL_SCISSOR_TEST);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
	pop_fx_debug(renderer);
}

static bool fx_render_subtexture_with_matrix(
		struct wlr_renderer *wlr_renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *box, const float matrix[static 9],
		float alpha) {
	struct fx_renderer *renderer = fx_get_renderer_in_context(wlr_renderer);
	struct fx_texture *texture = fx_get_texture(wlr_texture);
	assert(texture->fx_renderer == renderer);

	struct tex_shader *shader = NULL;

	switch (texture->target) {
	case GL_TEXTURE_2D:
		if (texture->has_alpha) {
			shader = &renderer->shaders.tex_rgba;
		} else {
			shader = &renderer->shaders.tex_rgbx;
		}
		break;
	case GL_TEXTURE_EXTERNAL_OES:
		// EGL_EXT_image_dma_buf_import_modifiers requires
		// GL_OES_EGL_image_external
		assert(renderer->exts.OES_egl_image_external);
		shader = &renderer->shaders.tex_ext;
		break;
	default:
		wlr_log(WLR_ERROR, "Aborting render");
		abort();
	}

	float gl_matrix[9];
	wlr_matrix_multiply(gl_matrix, renderer->projection, matrix);

	push_fx_debug(renderer);

	if (!texture->has_alpha && alpha == 1.0) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->tex);

	glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glUseProgram(shader->program);

	glUniformMatrix3fv(shader->proj, 1, GL_FALSE, gl_matrix);
	glUniform1i(shader->tex, 0);
	glUniform1f(shader->alpha, alpha);
	glUniform2f(shader->size, box->width, box->height);
	glUniform2f(shader->position, box->x, box->y);
	glUniform1f(shader->radius, 0);
	glUniform1f(shader->discard_transparent, false);

	float tex_matrix[9];
	wlr_matrix_identity(tex_matrix);
	wlr_matrix_translate(tex_matrix, box->x / texture->wlr_texture.width,
		box->y / texture->wlr_texture.height);
	wlr_matrix_scale(tex_matrix, box->width / texture->wlr_texture.width,
		box->height / texture->wlr_texture.height);
	glUniformMatrix3fv(shader->tex_proj, 1, GL_FALSE, tex_matrix);

	glVertexAttribPointer(shader->pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);

	glEnableVertexAttribArray(shader->pos_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(shader->pos_attrib);

	glBindTexture(texture->target, 0);

	pop_fx_debug(renderer);
	return true;
}

static void fx_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	struct fx_renderer *renderer = fx_get_renderer_in_context(wlr_renderer);

	float gl_matrix[9];
	wlr_matrix_multiply(gl_matrix, renderer->projection, matrix);

	push_fx_debug(renderer);

	if (color[3] == 1.0) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}

	glUseProgram(renderer->shaders.quad.program);

	glUniformMatrix3fv(renderer->shaders.quad.proj, 1, GL_FALSE, gl_matrix);
	glUniform4f(renderer->shaders.quad.color, color[0], color[1], color[2], color[3]);

	glVertexAttribPointer(renderer->shaders.quad.pos_attrib, 2, GL_FLOAT, GL_FALSE,
			0, verts);

	glEnableVertexAttribArray(renderer->shaders.quad.pos_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(renderer->shaders.quad.pos_attrib);

	pop_fx_debug(renderer);
}

static const uint32_t *fx_get_shm_texture_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	return get_fx_shm_formats(renderer, len);
}

static const struct wlr_drm_format_set *fx_get_dmabuf_texture_formats(
		struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	return wlr_egl_get_dmabuf_texture_formats(renderer->egl);
}

static const struct wlr_drm_format_set *fx_get_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	return wlr_egl_get_dmabuf_render_formats(renderer->egl);
}

static uint32_t fx_preferred_read_format(
		struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer =
		fx_get_renderer_in_context(wlr_renderer);

	push_fx_debug(renderer);

	GLint gl_format = -1, gl_type = -1, alpha_size = -1;
	glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &gl_format);
	glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &gl_type);
	glGetIntegerv(GL_ALPHA_BITS, &alpha_size);

	pop_fx_debug(renderer);

	const struct fx_pixel_format *fmt =
		get_fx_format_from_gl(gl_format, gl_type, alpha_size > 0);
	if (fmt != NULL) {
		return fmt->drm_format;
	}

	if (renderer->exts.EXT_read_format_bgra) {
		return DRM_FORMAT_XRGB8888;
	}
	return DRM_FORMAT_XBGR8888;
}

static bool fx_read_pixels(struct wlr_renderer *wlr_renderer,
		uint32_t drm_format, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	struct fx_renderer *renderer =
		fx_get_renderer_in_context(wlr_renderer);

	const struct fx_pixel_format *fmt =
		get_fx_format_from_drm(drm_format);
	if (fmt == NULL || !is_fx_pixel_format_supported(renderer, fmt)) {
		wlr_log(WLR_ERROR, "Cannot read pixels: unsupported pixel format 0x%"PRIX32, drm_format);
		return false;
	}

	if (fmt->gl_format == GL_BGRA_EXT && !renderer->exts.EXT_read_format_bgra) {
		wlr_log(WLR_ERROR,
			"Cannot read pixels: missing GL_EXT_read_format_bgra extension");
		return false;
	}

	const struct wlr_pixel_format_info *drm_fmt =
		drm_get_pixel_format_info(fmt->drm_format);
	assert(drm_fmt);
	if (pixel_format_info_pixels_per_block(drm_fmt) != 1) {
		wlr_log(WLR_ERROR, "Cannot read pixels: block formats are not supported");
		return false;
	}

	push_fx_debug(renderer);

	// Make sure any pending drawing is finished before we try to read it
	glFinish();

	glGetError(); // Clear the error flag

	unsigned char *p = (unsigned char *)data + dst_y * stride;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	uint32_t pack_stride = pixel_format_info_min_stride(drm_fmt, width);
	if (pack_stride == stride && dst_x == 0) {
		// Under these particular conditions, we can read the pixels with only
		// one glReadPixels call

		glReadPixels(src_x, src_y, width, height, fmt->gl_format, fmt->gl_type, p);
	} else {
		// Unfortunately GLES2 doesn't support GL_PACK_ROW_LENGTH, so we have to read
		// the lines out row by row
		for (size_t i = 0; i < height; ++i) {
			uint32_t y = src_y + i;
			glReadPixels(src_x, y, width, 1, fmt->gl_format,
				fmt->gl_type, p + i * stride + dst_x * drm_fmt->bytes_per_block);
		}
	}

	pop_fx_debug(renderer);

	return glGetError() == GL_NO_ERROR;
}

static int fx_get_drm_fd(struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer =
		fx_get_renderer(wlr_renderer);

	if (renderer->drm_fd < 0) {
		renderer->drm_fd = wlr_egl_dup_drm_fd(renderer->egl);
	}

	return renderer->drm_fd;
}

static uint32_t fx_get_render_buffer_caps(struct wlr_renderer *wlr_renderer) {
	return WLR_BUFFER_CAP_DMABUF;
}

static void fx_renderer_destroy(struct wlr_renderer *wlr_renderer) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);

	wlr_egl_make_current(renderer->egl);

	struct fx_framebuffer *fx_buffer, *fx_buffer_tmp;
	wl_list_for_each_safe(fx_buffer, fx_buffer_tmp, &renderer->buffers, link) {
		fx_framebuffer_destroy(fx_buffer);
	}

	struct fx_texture *tex, *tex_tmp;
	wl_list_for_each_safe(tex, tex_tmp, &renderer->textures, link) {
		fx_texture_destroy(tex);
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

	if (renderer->drm_fd >= 0) {
		close(renderer->drm_fd);
	}

	free(renderer);
}

static struct wlr_render_pass *begin_buffer_pass(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer, const struct wlr_buffer_pass_options *options) {
	struct fx_gles_render_pass *pass =
		fx_renderer_begin_buffer_pass(wlr_renderer, wlr_buffer, NULL, options);
	if (!pass) {
		return NULL;
	}
	return &pass->base;
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
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(renderer->egl);
	renderer->procs.glGenQueriesEXT(1, &timer->id);
	wlr_egl_restore_context(&prev_ctx);

	return &timer->base;
}

static int fx_get_render_time(struct wlr_render_timer *wlr_timer) {
	struct fx_render_timer *timer = fx_get_render_timer(wlr_timer);
	struct fx_renderer *renderer = timer->renderer;

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(renderer->egl);

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
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(renderer->egl);
	renderer->procs.glDeleteQueriesEXT(1, &timer->id);
	wlr_egl_restore_context(&prev_ctx);
	free(timer);
}

static const struct wlr_renderer_impl renderer_impl = {
	.destroy = fx_renderer_destroy,
	.bind_buffer = fx_bind_buffer,
	.begin = fx_renderer_begin,
	.end = fx_renderer_end,
	.clear = fx_renderer_clear,
	.scissor = fx_renderer_scissor,
	.render_subtexture_with_matrix = fx_render_subtexture_with_matrix,
	.render_quad_with_matrix = fx_render_quad_with_matrix,
	.get_shm_texture_formats = fx_get_shm_texture_formats,
	.get_dmabuf_texture_formats = fx_get_dmabuf_texture_formats,
	.get_render_formats = fx_get_render_formats,
	.preferred_read_format = fx_preferred_read_format,
	.read_pixels = fx_read_pixels,
	.get_drm_fd = fx_get_drm_fd,
	.get_render_buffer_caps = fx_get_render_buffer_caps,
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
	// quad fragment shader
	if (!link_quad_program(&renderer->shaders.quad)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}

	// rounded quad fragment shaders
	if (!link_quad_round_program(&renderer->shaders.quad_round, SHADER_SOURCE_QUAD_ROUND)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}
	// rounded quad fragment shaders
	if (!link_quad_round_program(&renderer->shaders.quad_round_tl, SHADER_SOURCE_QUAD_ROUND_TOP_LEFT)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}
	// rounded quad fragment shaders
	if (!link_quad_round_program(&renderer->shaders.quad_round_tr, SHADER_SOURCE_QUAD_ROUND_TOP_RIGHT)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}
	// rounded quad fragment shaders
	if (!link_quad_round_program(&renderer->shaders.quad_round_bl, SHADER_SOURCE_QUAD_ROUND_BOTTOM_LEFT)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}
	// rounded quad fragment shaders
	if (!link_quad_round_program(&renderer->shaders.quad_round_br, SHADER_SOURCE_QUAD_ROUND_BOTTOM_RIGHT)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}

	// fragment shaders
	if (!link_tex_program(&renderer->shaders.tex_rgba, SHADER_SOURCE_TEXTURE_RGBA)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_rgbx, SHADER_SOURCE_TEXTURE_RGBX)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_ext, SHADER_SOURCE_TEXTURE_EXTERNAL)) {
		wlr_log(WLR_ERROR, "Could not link tex_EXTERNAL shader");
		goto error;
	}

	// border corner shader
	if (!link_rounded_border_corner_program(&renderer->shaders.rounded_border_corner)) {
		wlr_log(WLR_ERROR, "Could not link rounded border corner shader");
		goto error;
	}

	// stencil mask shader
	if (!link_stencil_mask_program(&renderer->shaders.stencil_mask)) {
		wlr_log(WLR_ERROR, "Could not link stencil mask shader");
		goto error;
	}
	// box shadow shader
	if (!link_box_shadow_program(&renderer->shaders.box_shadow)) {
		wlr_log(WLR_ERROR, "Could not link box shadow shader");
		goto error;
	}

	// Blur shaders
	if (!link_blur1_program(&renderer->shaders.blur1)) {
		wlr_log(WLR_ERROR, "Could not link blur1 shader");
		goto error;
	}
	if (!link_blur2_program(&renderer->shaders.blur2)) {
		wlr_log(WLR_ERROR, "Could not link blur2 shader");
		goto error;
	}
	if (!link_blur_effects_program(&renderer->shaders.blur_effects)) {
		wlr_log(WLR_ERROR, "Could not link blur_effects shader");
		goto error;
	}

	return true;

error:
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.quad_round.program);
	glDeleteProgram(renderer->shaders.quad_round_tl.program);
	glDeleteProgram(renderer->shaders.quad_round_tr.program);
	glDeleteProgram(renderer->shaders.quad_round_bl.program);
	glDeleteProgram(renderer->shaders.quad_round_br.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);
	glDeleteProgram(renderer->shaders.rounded_border_corner.program);
	glDeleteProgram(renderer->shaders.stencil_mask.program);
	glDeleteProgram(renderer->shaders.box_shadow.program);
	glDeleteProgram(renderer->shaders.blur1.program);
	glDeleteProgram(renderer->shaders.blur2.program);
	glDeleteProgram(renderer->shaders.blur_effects.program);

	return false;
}

struct wlr_renderer *fx_renderer_create_egl(struct wlr_egl *egl) {
	if (!wlr_egl_make_current(egl)) {
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
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

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
		load_gl_proc(&renderer->procs.glGetInteger64vEXT, "glGetInteger64vEXT");
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
