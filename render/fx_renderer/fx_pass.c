#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pixman.h>
#include <time.h>
#include <unistd.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/util/transform.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "render/egl.h"
#include "render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/shaders.h"
#include "render/pass.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"
#include "scenefx/render/fx_renderer/fx_effect_framebuffers.h"
#include "scenefx/types/fx/corner_location.h"
#include "scenefx/types/fx/blur_data.h"
#include "util/matrix.h"

#define MAX_QUADS 86 // 4kb

struct fx_render_texture_options fx_render_texture_options_default(
		const struct wlr_render_texture_options *base) {
	struct fx_render_texture_options options = {
		.corner_radius = 0,
		.corners = CORNER_LOCATION_NONE,
		.discard_transparent = false,
		.clip_box = NULL,
	};
	memcpy(&options.base, base, sizeof(*base));
	return options;
}

struct fx_render_rect_options fx_render_rect_options_default(
		const struct wlr_render_rect_options *base) {
	struct fx_render_rect_options options = {
		.base = *base,
		.clipped_region = {
			.area = { .0, .0, .0, .0 },
			.corner_radius = 0,
			.corners = CORNER_LOCATION_NONE,
		},
	};
	return options;
}

// Gets a non-transformed wlr_box
static struct wlr_box get_monitor_box(struct wlr_output *output) {
	return (struct wlr_box) { 0, 0, output->width, output->height };
}

///
/// Base Wlroots pass functions
///

static const struct wlr_render_pass_impl render_pass_impl;

static struct fx_gles_render_pass *get_render_pass(struct wlr_render_pass *wlr_pass) {
	assert(wlr_pass->impl == &render_pass_impl);
	struct fx_gles_render_pass *pass = wl_container_of(wlr_pass, pass, base);
	return pass;
}

static bool render_pass_submit(struct wlr_render_pass *wlr_pass) {
	struct fx_gles_render_pass *pass = get_render_pass(wlr_pass);
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct fx_render_timer *timer = pass->timer;
	bool ok = false;

	push_fx_debug(renderer);

	if (timer) {
		// clear disjoint flag
		GLint64 disjoint;
		renderer->procs.glGetInteger64vEXT(GL_GPU_DISJOINT_EXT, &disjoint);
		// set up the query
		renderer->procs.glQueryCounterEXT(timer->id, GL_TIMESTAMP_EXT);
		// get end-of-CPU-work time in GL time domain
		renderer->procs.glGetInteger64vEXT(GL_TIMESTAMP_EXT, &timer->gl_cpu_end);
		// get end-of-CPU-work time in CPU time domain
		clock_gettime(CLOCK_MONOTONIC, &timer->cpu_end);
	}

	if (pass->signal_timeline != NULL) {
		EGLSyncKHR sync = wlr_egl_create_sync(renderer->egl, -1);
		if (sync == EGL_NO_SYNC_KHR) {
			goto out;
		}

		int sync_file_fd = wlr_egl_dup_fence_fd(renderer->egl, sync);
		wlr_egl_destroy_sync(renderer->egl, sync);
		if (sync_file_fd < 0) {
			goto out;
		}

		ok = wlr_drm_syncobj_timeline_import_sync_file(pass->signal_timeline, pass->signal_point, sync_file_fd);
		close(sync_file_fd);
		if (!ok) {
			goto out;
		}
	} else {
		glFlush();
	}

	ok = true;

out:
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	pop_fx_debug(renderer);
	wlr_egl_restore_context(&pass->prev_ctx);

	wlr_drm_syncobj_timeline_unref(pass->signal_timeline);
	wlr_buffer_unlock(pass->buffer->buffer);
	struct fx_effect_framebuffers *fbos = fx_effect_framebuffers_try_get(pass->output);
	if (fbos) {
		pixman_region32_fini(&fbos->blur_padding_region);
	}
	free(pass);

	return ok;
}

static void render_pass_add_texture(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_texture_options *options) {
	struct fx_gles_render_pass *pass = get_render_pass(wlr_pass);
	const struct fx_render_texture_options fx_options =
		fx_render_texture_options_default(options);
	// Re-use fx function but with default options
	fx_render_pass_add_texture(pass, &fx_options);
}

static void render_pass_add_rect(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_rect_options *options) {
	struct fx_gles_render_pass *pass = get_render_pass(wlr_pass);
	const struct fx_render_rect_options fx_options =
		fx_render_rect_options_default(options);
	// Re-use fx function but with default options
	fx_render_pass_add_rect(pass, &fx_options);
}

static const struct wlr_render_pass_impl render_pass_impl = {
	.submit = render_pass_submit,
	.add_texture = render_pass_add_texture,
	.add_rect = render_pass_add_rect,
};

///
/// FX pass functions
///

// TODO: REMOVE STENCILING

// Initialize the stenciling work
static void stencil_mask_init(void) {
	glClearStencil(0);
	glClear(GL_STENCIL_BUFFER_BIT);
	glEnable(GL_STENCIL_TEST);

	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	// Disable writing to color buffer
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
}

// Close the mask
static void stencil_mask_close(bool draw_inside_mask) {
	// Reenable writing to color buffer
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (draw_inside_mask) {
		glStencilFunc(GL_EQUAL, 1, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		return;
	}
	glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

// Finish stenciling and clear the buffer
static void stencil_mask_fini(void) {
	glClearStencil(0);
	glClear(GL_STENCIL_BUFFER_BIT);
	glDisable(GL_STENCIL_TEST);
}

static void render(const struct wlr_box *box, const pixman_region32_t *clip, GLint attrib) {
	pixman_region32_t region;
	pixman_region32_init_rect(&region, box->x, box->y, box->width, box->height);

	if (clip) {
		pixman_region32_intersect(&region, &region, clip);
	}

	int rects_len;
	const pixman_box32_t *rects = pixman_region32_rectangles(&region, &rects_len);
	if (rects_len == 0) {
		pixman_region32_fini(&region);
		return;
	}

	glEnableVertexAttribArray(attrib);

	for (int i = 0; i < rects_len;) {
		int batch = rects_len - i < MAX_QUADS ? rects_len - i : MAX_QUADS;
		int batch_end = batch + i;

		size_t vert_index = 0;
		GLfloat verts[MAX_QUADS * 6 * 2];
		for (; i < batch_end; i++) {
			const pixman_box32_t *rect = &rects[i];

			verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
			verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
			verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
			verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
			verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
			verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
			verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
			verts[vert_index++] = (GLfloat)(rect->y1 - box->y) / box->height;
			verts[vert_index++] = (GLfloat)(rect->x2 - box->x) / box->width;
			verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
			verts[vert_index++] = (GLfloat)(rect->x1 - box->x) / box->width;
			verts[vert_index++] = (GLfloat)(rect->y2 - box->y) / box->height;
		}

		glVertexAttribPointer(attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);
		glDrawArrays(GL_TRIANGLES, 0, batch * 6);
	}

	glDisableVertexAttribArray(attrib);

	pixman_region32_fini(&region);
}

static void set_proj_matrix(GLint loc, float proj[9], const struct wlr_box *box) {
	float gl_matrix[9];
	wlr_matrix_identity(gl_matrix);
	wlr_matrix_translate(gl_matrix, box->x, box->y);
	wlr_matrix_scale(gl_matrix, box->width, box->height);
	wlr_matrix_multiply(gl_matrix, proj, gl_matrix);
	glUniformMatrix3fv(loc, 1, GL_FALSE, gl_matrix);
}

static void set_tex_matrix(GLint loc, enum wl_output_transform trans,
		const struct wlr_fbox *box) {
	float tex_matrix[9];
	wlr_matrix_identity(tex_matrix);
	wlr_matrix_translate(tex_matrix, box->x, box->y);
	wlr_matrix_scale(tex_matrix, box->width, box->height);
	wlr_matrix_translate(tex_matrix, .5, .5);

	// since textures have a different origin point we have to transform
	// differently if we are rotating
	if (trans & WL_OUTPUT_TRANSFORM_90) {
		wlr_matrix_transform(tex_matrix, wlr_output_transform_invert(trans));
	} else {
		wlr_matrix_transform(tex_matrix, trans);
	}
	wlr_matrix_translate(tex_matrix, -.5, -.5);

	glUniformMatrix3fv(loc, 1, GL_FALSE, tex_matrix);
}

static void setup_blending(enum wlr_render_blend_mode mode) {
	switch (mode) {
	case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:
		glEnable(GL_BLEND);
		break;
	case WLR_RENDER_BLEND_MODE_NONE:
		glDisable(GL_BLEND);
		break;
	}
}

void fx_render_pass_add_texture(struct fx_gles_render_pass *pass,
		const struct fx_render_texture_options *fx_options) {
	const struct wlr_render_texture_options *options = &fx_options->base;
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct fx_texture *texture = fx_get_texture(options->texture);

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
		abort();
	}

	struct wlr_box dst_box;
	struct wlr_fbox src_fbox;
	wlr_render_texture_options_get_src_box(options, &src_fbox);
	wlr_render_texture_options_get_dst_box(options, &dst_box);
	float alpha = wlr_render_texture_options_get_alpha(options);

	const struct wlr_box *clip_box = &dst_box;
	if (!wlr_box_empty(fx_options->clip_box)) {
		clip_box = fx_options->clip_box;
	}

	src_fbox.x /= options->texture->width;
	src_fbox.y /= options->texture->height;
	src_fbox.width /= options->texture->width;
	src_fbox.height /= options->texture->height;

	push_fx_debug(renderer);

	if (options->wait_timeline != NULL) {
		int sync_file_fd =
			wlr_drm_syncobj_timeline_export_sync_file(options->wait_timeline, options->wait_point);
		if (sync_file_fd < 0) {
			return;
		}

		EGLSyncKHR sync = wlr_egl_create_sync(renderer->egl, sync_file_fd);
		close(sync_file_fd);
		if (sync == EGL_NO_SYNC_KHR) {
			return;
		}

		bool ok = wlr_egl_wait_sync(renderer->egl, sync);
		wlr_egl_destroy_sync(renderer->egl, sync);
		if (!ok) {
			return;
		}
	}

	bool has_alpha = texture->has_alpha
		|| alpha < 1.0
		|| fx_options->corner_radius > 0
		|| fx_options->discard_transparent;
	setup_blending(!has_alpha ? WLR_RENDER_BLEND_MODE_NONE : options->blend_mode);

	glUseProgram(shader->program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->tex);

	switch (options->filter_mode) {
	case WLR_SCALE_FILTER_BILINEAR:
		glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	case WLR_SCALE_FILTER_NEAREST:
		glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		break;
	}

	enum corner_location corners = fx_options->corners;

	glUniform1i(shader->tex, 0);
	glUniform1f(shader->alpha, alpha);
	glUniform2f(shader->size, clip_box->width, clip_box->height);
	glUniform2f(shader->position, clip_box->x, clip_box->y);
	glUniform1f(shader->discard_transparent, fx_options->discard_transparent);
	glUniform1f(shader->radius_top_left, (CORNER_LOCATION_TOP_LEFT & corners) == CORNER_LOCATION_TOP_LEFT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader->radius_top_right, (CORNER_LOCATION_TOP_RIGHT & corners) == CORNER_LOCATION_TOP_RIGHT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader->radius_bottom_left, (CORNER_LOCATION_BOTTOM_LEFT & corners) == CORNER_LOCATION_BOTTOM_LEFT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader->radius_bottom_right, (CORNER_LOCATION_BOTTOM_RIGHT & corners) == CORNER_LOCATION_BOTTOM_RIGHT ?
			fx_options->corner_radius : 0);

	set_proj_matrix(shader->proj, pass->projection_matrix, &dst_box);
	set_tex_matrix(shader->tex_proj, options->transform, &src_fbox);

	render(&dst_box, options->clip, shader->pos_attrib);

	glBindTexture(texture->target, 0);
	pop_fx_debug(renderer);
}

void fx_render_pass_add_rect(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options) {
	const struct wlr_render_rect_options *options = &fx_options->base;

	struct fx_renderer *renderer = pass->buffer->renderer;

	const struct wlr_render_color *color = &options->color;
	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->buffer->buffer, &box);

	pixman_region32_t clip_region;
	if (options->clip) {
		pixman_region32_init(&clip_region);
		pixman_region32_copy(&clip_region, options->clip);
	} else {
		pixman_region32_init_rect(&clip_region, box.x, box.y, box.width, box.height);
	}
	const struct wlr_box clipped_region_box = fx_options->clipped_region.area;
	enum corner_location clipped_region_corners = fx_options->clipped_region.corners;
	int clipped_region_corner_radius = clipped_region_corners != CORNER_LOCATION_NONE ?
		fx_options->clipped_region.corner_radius : 0;
	if (!wlr_box_empty(&clipped_region_box)) {
		pixman_region32_t user_clip_region;
		pixman_region32_init_rect(
			&user_clip_region,
			clipped_region_box.x + clipped_region_corner_radius * 0.3,
			clipped_region_box.y + clipped_region_corner_radius * 0.3,
			fmax(clipped_region_box.width - clipped_region_corner_radius * 0.6, 0),
			fmax(clipped_region_box.height - clipped_region_corner_radius * 0.6, 0)
		);
		pixman_region32_subtract(&clip_region, &clip_region, &user_clip_region);
		pixman_region32_fini(&user_clip_region);

		push_fx_debug(renderer);
		setup_blending(options->blend_mode);
	} else {
		push_fx_debug(renderer);
		setup_blending(color->a == 1.0 ? WLR_RENDER_BLEND_MODE_NONE : options->blend_mode);
	}

	struct quad_shader shader = renderer->shaders.quad;
	glUseProgram(shader.program);
	set_proj_matrix(shader.proj, pass->projection_matrix, &box);
	glUniform4f(shader.color, color->r, color->g, color->b, color->a);
	glUniform2f(shader.clip_size, clipped_region_box.width, clipped_region_box.height);
	glUniform2f(shader.clip_position, clipped_region_box.x, clipped_region_box.y);
	glUniform1f(shader.clip_radius_top_left, (CORNER_LOCATION_TOP_LEFT & clipped_region_corners) == CORNER_LOCATION_TOP_LEFT ?
			clipped_region_corner_radius : 0);
	glUniform1f(shader.clip_radius_top_right, (CORNER_LOCATION_TOP_RIGHT & clipped_region_corners) == CORNER_LOCATION_TOP_RIGHT ?
			clipped_region_corner_radius : 0);
	glUniform1f(shader.clip_radius_bottom_left, (CORNER_LOCATION_BOTTOM_LEFT & clipped_region_corners) == CORNER_LOCATION_BOTTOM_LEFT ?
			clipped_region_corner_radius : 0);
	glUniform1f(shader.clip_radius_bottom_right, (CORNER_LOCATION_BOTTOM_RIGHT & clipped_region_corners) == CORNER_LOCATION_BOTTOM_RIGHT ?
			clipped_region_corner_radius : 0);

	render(&box, &clip_region, renderer->shaders.quad.pos_attrib);
	pixman_region32_fini(&clip_region);

	pop_fx_debug(renderer);
}

void fx_render_pass_add_rect_grad(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_grad_options *fx_options) {
	const struct wlr_render_rect_options *options = &fx_options->base;

	struct fx_renderer *renderer = pass->buffer->renderer;

	if (renderer->shaders.quad_grad.max_len <= fx_options->gradient.count) {
		EGLint client_version;
		eglQueryContext(renderer->egl->display, renderer->egl->context,
			EGL_CONTEXT_CLIENT_VERSION, &client_version);

		glDeleteProgram(renderer->shaders.quad_grad.program);
		if (!link_quad_grad_program(&renderer->shaders.quad_grad, (GLint) client_version, fx_options->gradient.count + 1)) {
			wlr_log(WLR_ERROR, "Could not link quad shader after updating max_len to %d. Aborting renderer", fx_options->gradient.count + 1);
			abort();
		}
	}

	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->buffer->buffer, &box);

	push_fx_debug(renderer);
	setup_blending(options->blend_mode);

	glUseProgram(renderer->shaders.quad_grad.program);

	set_proj_matrix(renderer->shaders.quad_grad.proj, pass->projection_matrix, &box);
	glUniform4fv(renderer->shaders.quad_grad.colors, fx_options->gradient.count, (GLfloat*)fx_options->gradient.colors);
	glUniform1i(renderer->shaders.quad_grad.count, fx_options->gradient.count);
	glUniform2f(renderer->shaders.quad_grad.size, fx_options->gradient.range.width, fx_options->gradient.range.height);
	glUniform1f(renderer->shaders.quad_grad.degree, fx_options->gradient.degree);
	glUniform1f(renderer->shaders.quad_grad.linear, fx_options->gradient.linear);
	glUniform1f(renderer->shaders.quad_grad.blend, fx_options->gradient.blend);
	glUniform2f(renderer->shaders.quad_grad.grad_box, fx_options->gradient.range.x, fx_options->gradient.range.y);
	glUniform2f(renderer->shaders.quad_grad.origin, fx_options->gradient.origin[0], fx_options->gradient.origin[1]);

	render(&box, options->clip, renderer->shaders.quad_grad.pos_attrib);

	pop_fx_debug(renderer);
}

void fx_render_pass_add_rounded_rect(struct fx_gles_render_pass *pass,
		const struct fx_render_rounded_rect_options *fx_options) {
	const struct wlr_render_rect_options *options = &fx_options->base;

	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->buffer->buffer, &box);
	assert(box.width > 0 && box.height > 0);

	struct fx_renderer *renderer = pass->buffer->renderer;

	const struct wlr_render_color *color = &options->color;

	pixman_region32_t clip_region;
	if (options->clip) {
		pixman_region32_init(&clip_region);
		pixman_region32_copy(&clip_region, options->clip);
	} else {
		pixman_region32_init_rect(&clip_region, box.x, box.y, box.width, box.height);
	}
	const struct wlr_box clipped_region_box = fx_options->clipped_region.area;
	enum corner_location clipped_region_corners = fx_options->clipped_region.corners;
	int clipped_region_corner_radius = clipped_region_corners != CORNER_LOCATION_NONE ?
		fx_options->clipped_region.corner_radius : 0;
	if (!wlr_box_empty(&clipped_region_box)) {
		pixman_region32_t user_clip_region;
		pixman_region32_init_rect(
			&user_clip_region,
			clipped_region_box.x + clipped_region_corner_radius * 0.3,
			clipped_region_box.y + clipped_region_corner_radius * 0.3,
			fmax(clipped_region_box.width - clipped_region_corner_radius * 0.6, 0),
			fmax(clipped_region_box.height - clipped_region_corner_radius * 0.6, 0)
		);
		pixman_region32_subtract(&clip_region, &clip_region, &user_clip_region);
		pixman_region32_fini(&user_clip_region);
	}

	push_fx_debug(renderer);
	setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);

	struct quad_round_shader shader = renderer->shaders.quad_round;

	glUseProgram(shader.program);

	set_proj_matrix(shader.proj, pass->projection_matrix, &box);
	glUniform4f(shader.color, color->r, color->g, color->b, color->a);

	glUniform2f(shader.size, box.width, box.height);
	glUniform2f(shader.position, box.x, box.y);
	glUniform2f(shader.clip_size, clipped_region_box.width, clipped_region_box.height);
	glUniform2f(shader.clip_position, clipped_region_box.x, clipped_region_box.y);
	glUniform1f(shader.clip_radius_top_left, (CORNER_LOCATION_TOP_LEFT & clipped_region_corners) == CORNER_LOCATION_TOP_LEFT ?
			clipped_region_corner_radius : 0);
	glUniform1f(shader.clip_radius_top_right, (CORNER_LOCATION_TOP_RIGHT & clipped_region_corners) == CORNER_LOCATION_TOP_RIGHT ?
			clipped_region_corner_radius : 0);
	glUniform1f(shader.clip_radius_bottom_left, (CORNER_LOCATION_BOTTOM_LEFT & clipped_region_corners) == CORNER_LOCATION_BOTTOM_LEFT ?
			clipped_region_corner_radius : 0);
	glUniform1f(shader.clip_radius_bottom_right, (CORNER_LOCATION_BOTTOM_RIGHT & clipped_region_corners) == CORNER_LOCATION_BOTTOM_RIGHT ?
			clipped_region_corner_radius : 0);

	enum corner_location corners = fx_options->corners;
	glUniform1f(shader.radius_top_left, (CORNER_LOCATION_TOP_LEFT & corners) == CORNER_LOCATION_TOP_LEFT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader.radius_top_right, (CORNER_LOCATION_TOP_RIGHT & corners) == CORNER_LOCATION_TOP_RIGHT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader.radius_bottom_left, (CORNER_LOCATION_BOTTOM_LEFT & corners) == CORNER_LOCATION_BOTTOM_LEFT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader.radius_bottom_right, (CORNER_LOCATION_BOTTOM_RIGHT & corners) == CORNER_LOCATION_BOTTOM_RIGHT ?
			fx_options->corner_radius : 0);

	render(&box, &clip_region, renderer->shaders.quad_round.pos_attrib);
	pixman_region32_fini(&clip_region);

	pop_fx_debug(renderer);
}

void fx_render_pass_add_rounded_rect_grad(struct fx_gles_render_pass *pass,
		const struct fx_render_rounded_rect_grad_options *fx_options) {
	const struct wlr_render_rect_options *options = &fx_options->base;

	struct fx_renderer *renderer = pass->buffer->renderer;

	if (renderer->shaders.quad_grad_round.max_len <= fx_options->gradient.count) {
		EGLint client_version;
		eglQueryContext(renderer->egl->display, renderer->egl->context,
			EGL_CONTEXT_CLIENT_VERSION, &client_version);

		glDeleteProgram(renderer->shaders.quad_grad_round.program);
		if (!link_quad_grad_round_program(&renderer->shaders.quad_grad_round, (GLint) client_version, fx_options->gradient.count + 1)) {
			wlr_log(WLR_ERROR, "Could not link quad shader after updating max_len to %d. Aborting renderer", fx_options->gradient.count + 1);
			abort();
		}
	}

	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->buffer->buffer, &box);

	push_fx_debug(renderer);
	setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);

	struct quad_grad_round_shader shader = renderer->shaders.quad_grad_round;
	glUseProgram(shader.program);

	set_proj_matrix(shader.proj, pass->projection_matrix, &box);

	glUniform2f(shader.size, box.width, box.height);
	glUniform2f(shader.position, box.x, box.y);

	glUniform4fv(shader.colors, fx_options->gradient.count, (GLfloat*)fx_options->gradient.colors);
	glUniform1i(shader.count, fx_options->gradient.count);
	glUniform2f(shader.grad_size, fx_options->gradient.range.width, fx_options->gradient.range.height);
	glUniform1f(shader.degree, fx_options->gradient.degree);
	glUniform1f(shader.linear, fx_options->gradient.linear);
	glUniform1f(shader.blend, fx_options->gradient.blend);
	glUniform2f(shader.grad_box, fx_options->gradient.range.x, fx_options->gradient.range.y);
	glUniform2f(shader.origin, fx_options->gradient.origin[0], fx_options->gradient.origin[1]);

	enum corner_location corners = fx_options->corners;
	glUniform1f(shader.radius_top_left, (CORNER_LOCATION_TOP_LEFT & corners) == CORNER_LOCATION_TOP_LEFT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader.radius_top_right, (CORNER_LOCATION_TOP_RIGHT & corners) == CORNER_LOCATION_TOP_RIGHT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader.radius_bottom_left, (CORNER_LOCATION_BOTTOM_LEFT & corners) == CORNER_LOCATION_BOTTOM_LEFT ?
			fx_options->corner_radius : 0);
	glUniform1f(shader.radius_bottom_right, (CORNER_LOCATION_BOTTOM_RIGHT & corners) == CORNER_LOCATION_BOTTOM_RIGHT ?
			fx_options->corner_radius : 0);

	render(&box, options->clip, shader.pos_attrib);

	pop_fx_debug(renderer);
}

void fx_render_pass_add_box_shadow(struct fx_gles_render_pass *pass,
		const struct fx_render_box_shadow_options *options) {
	struct fx_renderer *renderer = pass->buffer->renderer;

	struct wlr_box box = options->box;
	assert(box.width > 0 && box.height > 0);

	pixman_region32_t clip_region;
	if (options->clip) {
		pixman_region32_init(&clip_region);
		pixman_region32_copy(&clip_region, options->clip);
	} else {
		pixman_region32_init_rect(&clip_region, box.x, box.y, box.width, box.height);
	}
	const struct wlr_box clipped_region_box = options->clipped_region.area;
	enum corner_location clipped_region_corners = options->clipped_region.corners;
	int clipped_region_corner_radius = clipped_region_corners != CORNER_LOCATION_NONE ?
		options->clipped_region.corner_radius : 0;
	if (!wlr_box_empty(&clipped_region_box)) {
		pixman_region32_t user_clip_region;
		pixman_region32_init_rect(
			&user_clip_region,
			clipped_region_box.x + clipped_region_corner_radius * 0.3,
			clipped_region_box.y + clipped_region_corner_radius * 0.3,
			fmax(clipped_region_box.width - clipped_region_corner_radius * 0.6, 0),
			fmax(clipped_region_box.height - clipped_region_corner_radius * 0.6, 0)
		);
		pixman_region32_subtract(&clip_region, &clip_region, &user_clip_region);
		pixman_region32_fini(&user_clip_region);
	}

	push_fx_debug(renderer);
	// blending will practically always be needed (unless we have a madman
	// who uses opaque shadows with zero sigma), so just enable it
	setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(renderer->shaders.box_shadow.program);

	const struct wlr_render_color *color = &options->color;
	set_proj_matrix(renderer->shaders.box_shadow.proj, pass->projection_matrix, &box);
	glUniform4f(renderer->shaders.box_shadow.color, color->r, color->g, color->b, color->a);
	glUniform1f(renderer->shaders.box_shadow.blur_sigma, options->blur_sigma);
	glUniform2f(renderer->shaders.box_shadow.size, box.width, box.height);
	glUniform2f(renderer->shaders.box_shadow.position, box.x, box.y);
	glUniform1f(renderer->shaders.box_shadow.corner_radius, options->corner_radius);

	glUniform1f(renderer->shaders.box_shadow.clip_radius_top_left,
			(CORNER_LOCATION_TOP_LEFT & clipped_region_corners) == CORNER_LOCATION_TOP_LEFT ?
			clipped_region_corner_radius : 0);
	glUniform1f(renderer->shaders.box_shadow.clip_radius_top_right,
			(CORNER_LOCATION_TOP_RIGHT & clipped_region_corners) == CORNER_LOCATION_TOP_RIGHT ?
			clipped_region_corner_radius : 0);
	glUniform1f(renderer->shaders.box_shadow.clip_radius_bottom_left,
			(CORNER_LOCATION_BOTTOM_LEFT & clipped_region_corners) == CORNER_LOCATION_BOTTOM_LEFT ?
			clipped_region_corner_radius : 0);
	glUniform1f(renderer->shaders.box_shadow.clip_radius_bottom_right,
			(CORNER_LOCATION_BOTTOM_RIGHT & clipped_region_corners) == CORNER_LOCATION_BOTTOM_RIGHT ?
			clipped_region_corner_radius : 0);

	glUniform2f(renderer->shaders.box_shadow.clip_position, clipped_region_box.x, clipped_region_box.y);
	glUniform2f(renderer->shaders.box_shadow.clip_size, clipped_region_box.width, clipped_region_box.height);

	render(&box, &clip_region, renderer->shaders.box_shadow.pos_attrib);
	pixman_region32_fini(&clip_region);

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	pop_fx_debug(renderer);
}

// Renders the blur for each damaged rect and swaps the buffer
static void render_blur_segments(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options, struct blur_shader* shader) {
	struct fx_render_texture_options *tex_options = &fx_options->tex_options;
	struct wlr_render_texture_options *options = &tex_options->base;
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct blur_data *blur_data = fx_options->blur_data;

	// Swap fbo
	if (fx_options->current_buffer == pass->fx_effect_framebuffers->effects_buffer) {
		fx_framebuffer_bind(pass->fx_effect_framebuffers->effects_buffer_swapped);
	} else {
		fx_framebuffer_bind(pass->fx_effect_framebuffers->effects_buffer);
	}

	options->texture = fx_texture_from_buffer(&renderer->wlr_renderer,
			fx_options->current_buffer->buffer);
	struct fx_texture *texture = fx_get_texture(options->texture);

	/*
	 * Render
	 */

	struct wlr_box dst_box;
	struct wlr_fbox src_fbox;
	wlr_render_texture_options_get_src_box(options, &src_fbox);
	wlr_render_texture_options_get_dst_box(options, &dst_box);
	src_fbox.x /= options->texture->width;
	src_fbox.y /= options->texture->height;
	src_fbox.width /= options->texture->width;
	src_fbox.height /= options->texture->height;

	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);

	push_fx_debug(renderer);

	glUseProgram(shader->program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->tex);

	switch (options->filter_mode) {
	case WLR_SCALE_FILTER_BILINEAR:
		glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	case WLR_SCALE_FILTER_NEAREST:
		abort();
	}

	glUniform1i(shader->tex, 0);
	glUniform1f(shader->radius, blur_data->radius);

	if (shader == &renderer->shaders.blur1) {
		glUniform2f(shader->halfpixel,
				0.5f / (options->texture->width / 2.0f),
				0.5f / (options->texture->height / 2.0f));
	} else {
		glUniform2f(shader->halfpixel,
				0.5f / (options->texture->width * 2.0f),
				0.5f / (options->texture->height * 2.0f));
	}

	set_proj_matrix(shader->proj, pass->projection_matrix, &dst_box);
	set_tex_matrix(shader->tex_proj, options->transform, &src_fbox);

	render(&dst_box, options->clip, shader->pos_attrib);

	glBindTexture(texture->target, 0);
	pop_fx_debug(renderer);

	wlr_texture_destroy(options->texture);

	// Swap buffer. We don't want to draw to the same buffer
	if (fx_options->current_buffer != pass->fx_effect_framebuffers->effects_buffer) {
		fx_options->current_buffer = pass->fx_effect_framebuffers->effects_buffer;
	} else {
		fx_options->current_buffer = pass->fx_effect_framebuffers->effects_buffer_swapped;
	}
}

static void render_blur_effects(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options) {
	struct fx_render_texture_options *tex_options = &fx_options->tex_options;
	struct wlr_render_texture_options *options = &tex_options->base;
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct blur_data *blur_data = fx_options->blur_data;
	struct fx_texture *texture = fx_get_texture(options->texture);

	struct blur_effects_shader shader = renderer->shaders.blur_effects;

	struct wlr_box dst_box;
	struct wlr_fbox src_fbox;
	wlr_render_texture_options_get_src_box(options, &src_fbox);
	wlr_render_texture_options_get_dst_box(options, &dst_box);

	src_fbox.x /= options->texture->width;
	src_fbox.y /= options->texture->height;
	src_fbox.width /= options->texture->width;
	src_fbox.height /= options->texture->height;

	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);

	push_fx_debug(renderer);

	glUseProgram(shader.program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->tex);

	switch (options->filter_mode) {
	case WLR_SCALE_FILTER_BILINEAR:
		glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	case WLR_SCALE_FILTER_NEAREST:
		abort();
	}

	glUniform1i(shader.tex, 0);
	glUniform1f(shader.noise, blur_data->noise);
	glUniform1f(shader.brightness, blur_data->brightness);
	glUniform1f(shader.contrast, blur_data->contrast);
	glUniform1f(shader.saturation, blur_data->saturation);

	set_proj_matrix(shader.proj, pass->projection_matrix, &dst_box);
	set_tex_matrix(shader.tex_proj, options->transform, &src_fbox);

	render(&dst_box, options->clip, shader.pos_attrib);

	glBindTexture(texture->target, 0);
	pop_fx_debug(renderer);

	wlr_texture_destroy(options->texture);
}

// Blurs the main_buffer content and returns the blurred framebuffer
static struct fx_framebuffer *get_main_buffer_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options) {
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct blur_data *blur_data = fx_options->blur_data;
	struct wlr_box monitor_box = get_monitor_box(pass->output);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_copy(&damage, fx_options->tex_options.base.clip);
	wlr_region_transform(&damage, &damage, fx_options->tex_options.base.transform,
			monitor_box.width, monitor_box.height);

	wlr_region_expand(&damage, &damage, blur_data_calc_size(fx_options->blur_data));

	// damage region will be scaled, make a temp
	pixman_region32_t scaled_damage;
	pixman_region32_init(&scaled_damage);

	fx_options->tex_options.base.src_box = (struct wlr_fbox) {
		monitor_box.x,
		monitor_box.y,
		monitor_box.width,
		monitor_box.height,
	};
	fx_options->tex_options.base.dst_box = monitor_box;
	// Clip the blur to the damage
	fx_options->tex_options.base.clip = &scaled_damage;
	// Artifacts with NEAREST filter
	fx_options->tex_options.base.filter_mode = WLR_SCALE_FILTER_BILINEAR;

	// Downscale
	for (int i = 0; i < blur_data->num_passes; ++i) {
		wlr_region_scale(&scaled_damage, &damage, 1.0f / (1 << (i + 1)));
		render_blur_segments(pass, fx_options, &renderer->shaders.blur1);
	}

	// Upscale
	for (int i = blur_data->num_passes - 1; i >= 0; --i) {
		// when upsampling we make the region twice as big
		wlr_region_scale(&scaled_damage, &damage, 1.0f / (1 << i));
		render_blur_segments(pass, fx_options, &renderer->shaders.blur2);
	}

	pixman_region32_fini(&scaled_damage);

	// Render additional blur effects like saturation, noise, contrast, etc...
	if (blur_data_should_parameters_blur_effects(blur_data)
			&& pixman_region32_not_empty(&damage)) {
		if (fx_options->current_buffer == pass->fx_effect_framebuffers->effects_buffer) {
			fx_framebuffer_bind(pass->fx_effect_framebuffers->effects_buffer_swapped);
		} else {
			fx_framebuffer_bind(pass->fx_effect_framebuffers->effects_buffer);
		}
		fx_options->tex_options.base.clip = &damage;
		fx_options->tex_options.base.texture = fx_texture_from_buffer(
				&renderer->wlr_renderer, fx_options->current_buffer->buffer);
		render_blur_effects(pass, fx_options);
		if (fx_options->current_buffer != pass->fx_effect_framebuffers->effects_buffer) {
			fx_options->current_buffer = pass->fx_effect_framebuffers->effects_buffer;
		} else {
			fx_options->current_buffer = pass->fx_effect_framebuffers->effects_buffer_swapped;
		}
	}

	pixman_region32_fini(&damage);

	// Bind back to the default buffer
	fx_framebuffer_bind(pass->buffer);

	return fx_options->current_buffer;
}

void fx_render_pass_add_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options) {
	if (pass->buffer->renderer->basic_renderer) {
		wlr_log(WLR_ERROR, "Please use 'fx_renderer_begin_buffer_pass' instead of "
				"'wlr_renderer_begin_buffer_pass' to use advanced effects");
		return;
	}
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct fx_render_texture_options *tex_options = &fx_options->tex_options;
	const struct wlr_render_texture_options *options = &tex_options->base;

	pixman_region32_t translucent_region;
	pixman_region32_init(&translucent_region);

	struct wlr_box dst_box;
	wlr_render_texture_options_get_dst_box(options, &dst_box);

	// Gets the translucent region
	pixman_box32_t surface_box = { 0, 0, dst_box.width, dst_box.height };
	pixman_region32_copy(&translucent_region, fx_options->opaque_region);
	pixman_region32_inverse(&translucent_region, &translucent_region, &surface_box);
	if (!pixman_region32_not_empty(&translucent_region)) {
		goto damage_finish;
	}

	struct fx_framebuffer *buffer = pass->fx_effect_framebuffers->optimized_blur_buffer;
	if (!buffer || !fx_options->use_optimized_blur) {
		if (!buffer) {
			wlr_log(WLR_ERROR, "Warning: Failed to use optimized blur");
		}
		pixman_region32_translate(&translucent_region, dst_box.x, dst_box.y);
		pixman_region32_intersect(&translucent_region, &translucent_region, options->clip);

		// Render the blur into its own buffer
		struct fx_render_blur_pass_options blur_options = *fx_options;
		blur_options.tex_options.base.clip = &translucent_region;
		blur_options.current_buffer = pass->buffer;
		buffer = get_main_buffer_blur(pass, &blur_options);
	}
	struct wlr_texture *wlr_texture =
		fx_texture_from_buffer(&renderer->wlr_renderer, buffer->buffer);
	struct fx_texture *blur_texture = fx_get_texture(wlr_texture);
	blur_texture->has_alpha = true;

	// Get a stencil of the window ignoring transparent regions
	if (fx_options->ignore_transparent && fx_options->tex_options.base.texture) {
		stencil_mask_init();

		struct fx_render_texture_options tex_options = fx_options->tex_options;
		tex_options.discard_transparent = true;
		fx_render_pass_add_texture(pass, &tex_options);

		stencil_mask_close(true);
	}

	// Draw the blurred texture
	tex_options->base.dst_box = get_monitor_box(pass->output);
	tex_options->base.src_box = (struct wlr_fbox) {
		.x = 0,
		.y = 0,
		.width = buffer->buffer->width,
		.height = buffer->buffer->height,
	};
	tex_options->base.texture = &blur_texture->wlr_texture;
	fx_render_pass_add_texture(pass, tex_options);

	wlr_texture_destroy(&blur_texture->wlr_texture);

	// Finish stenciling
	if (fx_options->ignore_transparent && fx_options->tex_options.base.texture) {
		stencil_mask_fini();
	}

damage_finish:
	pixman_region32_fini(&translucent_region);
}

bool fx_render_pass_add_optimized_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options) {
	if (pass->buffer->renderer->basic_renderer) {
		wlr_log(WLR_ERROR, "Please use 'fx_renderer_begin_buffer_pass' instead of "
				"'wlr_renderer_begin_buffer_pass' to use advanced effects");
		return false;
	}
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct wlr_box dst_box = fx_options->tex_options.base.dst_box;

	pixman_region32_t clip;
	pixman_region32_init_rect(&clip,
			dst_box.x, dst_box.y, dst_box.width, dst_box.height);

	// Render the blur into its own buffer
	struct fx_render_blur_pass_options blur_options = *fx_options;
	blur_options.current_buffer = pass->buffer;
	blur_options.tex_options.base.clip = &clip;
	struct fx_framebuffer *buffer = get_main_buffer_blur(pass, &blur_options);

	// Update the optimized blur buffer if invalid
	bool failed = false;
	fx_framebuffer_get_or_create_custom(renderer, pass->output, NULL,
			&pass->fx_effect_framebuffers->optimized_blur_buffer, &failed);
	if (failed) {
		goto finish;
	}

	// Render the newly blurred content into the blur_buffer
	fx_renderer_read_to_buffer(pass, &clip,
			pass->fx_effect_framebuffers->optimized_blur_buffer, buffer);

finish:
	pixman_region32_fini(&clip);
	return !failed;
}

// TODO: Use blitting for glesv3
void fx_renderer_read_to_buffer(struct fx_gles_render_pass *pass,
		pixman_region32_t *_region, struct fx_framebuffer *dst_buffer,
		struct fx_framebuffer *src_buffer) {
	if (!_region || !pixman_region32_not_empty(_region)) {
		return;
	}

	pixman_region32_t region;
	pixman_region32_init(&region);
	pixman_region32_copy(&region, _region);

	struct wlr_texture *src_tex =
		fx_texture_from_buffer(&pass->buffer->renderer->wlr_renderer, src_buffer->buffer);
	if (src_tex == NULL) {
		pixman_region32_fini(&region);
		return;
	}

	// Draw onto the dst_buffer
	fx_framebuffer_bind(dst_buffer);
	wlr_render_pass_add_texture(&pass->base, &(struct wlr_render_texture_options) {
		.texture = src_tex,
		.clip = &region,
		.transform = WL_OUTPUT_TRANSFORM_NORMAL,
		.blend_mode = WLR_RENDER_BLEND_MODE_NONE,
		.dst_box = (struct wlr_box){
			.width = dst_buffer->buffer->width,
			.height = dst_buffer->buffer->height,
		},
		.src_box = (struct wlr_fbox){
			.x = 0,
			.y = 0,
			.width = src_buffer->buffer->width,
			.height = src_buffer->buffer->height,
		},
	});
	wlr_texture_destroy(src_tex);

	// Bind back to the main WLR buffer
	fx_framebuffer_bind(pass->buffer);

	pixman_region32_fini(&region);
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

static struct fx_gles_render_pass *begin_buffer_pass(struct fx_framebuffer *buffer,
		struct wlr_egl_context *prev_ctx, struct fx_render_timer *timer,
		struct wlr_drm_syncobj_timeline *signal_timeline, uint64_t signal_point) {
	struct fx_renderer *renderer = buffer->renderer;
	struct wlr_buffer *wlr_buffer = buffer->buffer;

	if (renderer->procs.glGetGraphicsResetStatusKHR) {
		GLenum status = renderer->procs.glGetGraphicsResetStatusKHR();
		if (status != GL_NO_ERROR) {
			wlr_log(WLR_ERROR, "GPU reset (%s)", reset_status_str(status));
			wl_signal_emit_mutable(&renderer->wlr_renderer.events.lost, NULL);
			return NULL;
		}
	}

	GLint fbo = fx_framebuffer_get_fbo(buffer);
	if (!fbo) {
		return NULL;
	}

	struct fx_gles_render_pass *pass = calloc(1, sizeof(*pass));
	if (pass == NULL) {
		return NULL;
	}

	wlr_render_pass_init(&pass->base, &render_pass_impl);
	wlr_buffer_lock(wlr_buffer);
	pass->buffer = buffer;
	pass->timer = timer;
	pass->prev_ctx = *prev_ctx;
	if (signal_timeline != NULL) {
		pass->signal_timeline = wlr_drm_syncobj_timeline_ref(signal_timeline);
		pass->signal_point = signal_point;
	}

	matrix_projection(pass->projection_matrix, wlr_buffer->width, wlr_buffer->height,
		WL_OUTPUT_TRANSFORM_FLIPPED_180);

	push_fx_debug(renderer);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glViewport(0, 0, wlr_buffer->width, wlr_buffer->height);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_SCISSOR_TEST);
	pop_fx_debug(renderer);

	return pass;
}

struct fx_gles_render_pass *fx_renderer_begin_buffer_pass(
		struct wlr_renderer *wlr_renderer, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output, const struct fx_buffer_pass_options *fx_options) {
	const struct wlr_buffer_pass_options *options = fx_options->base;
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);

	renderer->basic_renderer = (output == NULL);

	struct wlr_egl_context prev_ctx = {0};
	if (!wlr_egl_make_current(renderer->egl, &prev_ctx)) {
		return NULL;
	}

	struct fx_render_timer *timer = NULL;
	if (options->timer) {
		timer = fx_get_render_timer(options->timer);
		clock_gettime(CLOCK_MONOTONIC, &timer->cpu_start);
	}

	struct fx_framebuffer *buffer = fx_framebuffer_get_or_create(renderer, wlr_buffer);
	if (!buffer) {
		return NULL;
	}

	// For per output framebuffers
	struct fx_effect_framebuffers *fbos = NULL;
	// Update the buffers if needed
	if (!renderer->basic_renderer) {
		bool failed = false;

		fbos = fx_effect_framebuffers_try_get(output);
		failed |= fbos == NULL;
		if (fbos) {
			pixman_region32_init(&fbos->blur_padding_region);

			fx_framebuffer_get_or_create_custom(renderer, output, fx_options->swapchain,
					&fbos->blur_saved_pixels_buffer, &failed);
			fx_framebuffer_get_or_create_custom(renderer, output, fx_options->swapchain,
					&fbos->effects_buffer, &failed);
			fx_framebuffer_get_or_create_custom(renderer, output, fx_options->swapchain,
					&fbos->effects_buffer_swapped, &failed);
		}

		if (failed) {
			// Revert back to not running advanced effects
			renderer->basic_renderer = true;
		}
	}

	struct fx_gles_render_pass *pass = begin_buffer_pass(buffer,
			&prev_ctx, timer, options->signal_timeline, options->signal_point);
	if (!pass) {
		return NULL;
	}
	pass->fx_effect_framebuffers = fbos;
	pass->output = output;
	return pass;
}
