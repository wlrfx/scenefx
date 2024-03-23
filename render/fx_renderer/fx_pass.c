#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pixman.h>
#include <time.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "render/egl.h"
#include "render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/matrix.h"
#include "render/pass.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"
#include "scenefx/render/fx_renderer/fx_effect_framebuffers.h"
#include "scenefx/types/fx/blur_data.h"
#include "scenefx/types/fx/shadow_data.h"

#define MAX_QUADS 86 // 4kb

struct fx_render_texture_options fx_render_texture_options_default(
		const struct wlr_render_texture_options *base) {
	struct fx_render_texture_options options = {
		.corner_radius = 0,
		.discard_transparent = false,
		.scale = 1.0f,
		.clip_box = NULL,
	};
	memcpy(&options.base, base, sizeof(*base));
	return options;
}

struct fx_render_rect_options fx_render_rect_options_default(
		const struct wlr_render_rect_options *base) {
	struct fx_render_rect_options options = {
		.scale = 1.0f,
	};
	memcpy(&options.base, base, sizeof(*base));
	return options;
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

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	pop_fx_debug(renderer);

	wlr_buffer_unlock(pass->buffer->buffer);
	free(pass);

	pixman_region32_fini(&renderer->blur_padding_region);

	return true;
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

// make sure the texture source box does not try and sample outside of the
// texture
static void check_tex_src_box(const struct wlr_render_texture_options *options) {
	if (!wlr_fbox_empty(&options->src_box)) {
		const struct wlr_fbox *box = &options->src_box;
		assert(box->x >= 0 && box->y >= 0 &&
			box->x + box->width <= options->texture->width &&
			box->y + box->height <= options->texture->height);
	}
}

void fx_render_pass_add_texture(struct fx_gles_render_pass *pass,
		const struct fx_render_texture_options *fx_options) {
	const struct wlr_render_texture_options *options = &fx_options->base;

	check_tex_src_box(options);

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

	struct wlr_box *clip_box = &dst_box;
	if (!wlr_box_empty(fx_options->clip_box)) {
		clip_box = fx_options->clip_box;
	}

	src_fbox.x /= options->texture->width;
	src_fbox.y /= options->texture->height;
	src_fbox.width /= options->texture->width;
	src_fbox.height /= options->texture->height;

	push_fx_debug(renderer);
	setup_blending(!texture->has_alpha && alpha == 1.0 ?
		WLR_RENDER_BLEND_MODE_NONE : options->blend_mode);

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

	glUniform1i(shader->tex, 0);
	glUniform1f(shader->alpha, alpha);
	glUniform2f(shader->size, clip_box->width, clip_box->height);
	glUniform2f(shader->position, clip_box->x, clip_box->y);
	glUniform1f(shader->radius, fx_options->corner_radius);
	glUniform1f(shader->discard_transparent, fx_options->discard_transparent);

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

	push_fx_debug(renderer);
	setup_blending(color->a == 1.0 ? WLR_RENDER_BLEND_MODE_NONE : options->blend_mode);

	glUseProgram(renderer->shaders.quad.program);

	set_proj_matrix(renderer->shaders.quad.proj, pass->projection_matrix, &box);
	glUniform4f(renderer->shaders.quad.color, color->r, color->g, color->b, color->a);

	render(&box, options->clip, renderer->shaders.quad.pos_attrib);

	pop_fx_debug(renderer);
}

void fx_render_pass_add_stencil_mask(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options, int corner_radius) {
	const struct wlr_render_rect_options *options = &fx_options->base;

	struct fx_renderer *renderer = pass->buffer->renderer;

	const struct wlr_render_color *color = &options->color;
	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->buffer->buffer, &box);
	assert(box.width > 0 && box.height > 0);

	push_fx_debug(renderer);
	setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);

	glUseProgram(renderer->shaders.stencil_mask.program);

	set_proj_matrix(renderer->shaders.stencil_mask.proj, pass->projection_matrix, &box);
	glUniform4f(renderer->shaders.stencil_mask.color, color->r, color->g, color->b, color->a);
	glUniform2f(renderer->shaders.stencil_mask.half_size, box.width * 0.5, box.height * 0.5);
	glUniform2f(renderer->shaders.stencil_mask.position, box.x, box.y);
	glUniform1f(renderer->shaders.stencil_mask.radius, corner_radius);

	render(&box, options->clip, renderer->shaders.stencil_mask.pos_attrib);

	pop_fx_debug(renderer);
}

void fx_render_pass_add_box_shadow(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options,
		int corner_radius, struct shadow_data *shadow_data) {
	const struct wlr_render_rect_options *options = &fx_options->base;

	struct fx_renderer *renderer = pass->buffer->renderer;

	const struct wlr_render_color *color = &shadow_data->color;
	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->buffer->buffer, &box);
	assert(box.width > 0 && box.height > 0);
	struct wlr_box surface_box = box;
	float blur_sigma = shadow_data->blur_sigma * fx_options->scale;

	// Extend the size of the box while also considering the shadow offset
	box.x -= blur_sigma - shadow_data->offset_x;
	box.y -= blur_sigma - shadow_data->offset_y;
	box.width += blur_sigma * 2;
	box.height += blur_sigma * 2;

	pixman_region32_t render_region;
	pixman_region32_init(&render_region);

	pixman_region32_t inner_region;
	pixman_region32_init(&inner_region);
	pixman_region32_union_rect(&inner_region, &inner_region,
			surface_box.x + corner_radius * 0.5,
			surface_box.y + corner_radius * 0.5,
			surface_box.width - corner_radius,
			surface_box.height - corner_radius);
	pixman_region32_subtract(&render_region, options->clip, &inner_region);
	pixman_region32_fini(&inner_region);

	push_fx_debug(renderer);

	// Init stencil work
	stencil_mask_init();
	// Draw the rounded rect as a mask
	fx_render_pass_add_stencil_mask(pass, fx_options, corner_radius);
	stencil_mask_close(false);

	// blending will practically always be needed (unless we have a madman
	// who uses opaque shadows with zero sigma), so just enable it
	setup_blending(WLR_RENDER_BLEND_MODE_PREMULTIPLIED);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(renderer->shaders.box_shadow.program);

	set_proj_matrix(renderer->shaders.box_shadow.proj, pass->projection_matrix, &box);
	glUniform4f(renderer->shaders.box_shadow.color, color->r, color->g, color->b, color->a);
	glUniform1f(renderer->shaders.box_shadow.blur_sigma, blur_sigma);
	glUniform1f(renderer->shaders.box_shadow.corner_radius, corner_radius);
	glUniform2f(renderer->shaders.box_shadow.size, box.width, box.height);
	glUniform2f(renderer->shaders.box_shadow.position, box.x, box.y);

	render(&box, &render_region, renderer->shaders.box_shadow.pos_attrib);
	pixman_region32_fini(&render_region);

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	stencil_mask_fini();

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

	check_tex_src_box(options);

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

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_copy(&damage, fx_options->tex_options.base.clip);
	wlr_region_transform(&damage, &damage, fx_options->tex_options.base.transform,
			fx_options->monitor_box.width, fx_options->monitor_box.height);

	wlr_region_expand(&damage, &damage, blur_data_calc_size(fx_options->blur_data));

	// damage region will be scaled, make a temp
	pixman_region32_t scaled_damage;
	pixman_region32_init(&scaled_damage);

	fx_options->tex_options.base.src_box = (struct wlr_fbox) {
		fx_options->monitor_box.x,
		fx_options->monitor_box.y,
		fx_options->monitor_box.width,
		fx_options->monitor_box.height,
	};
	fx_options->tex_options.base.dst_box = fx_options->monitor_box;
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
		abort();
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
	tex_options->base.dst_box = fx_options->monitor_box;
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

void fx_render_pass_add_optimized_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options) {
	if (pass->buffer->renderer->basic_renderer) {
		wlr_log(WLR_ERROR, "Please use 'fx_renderer_begin_buffer_pass' instead of "
				"'wlr_renderer_begin_buffer_pass' to use advanced effects");
		abort();
	}
	struct fx_renderer *renderer = pass->buffer->renderer;
	struct wlr_box monitor_box = fx_options->monitor_box;

	pixman_region32_t fake_damage;
	pixman_region32_init_rect(&fake_damage, 0, 0, monitor_box.width, monitor_box.height);

	// Render the blur into its own buffer
	struct fx_render_blur_pass_options blur_options = *fx_options;
	blur_options.tex_options.base.clip = &fake_damage;
	blur_options.current_buffer = pass->buffer;
	struct fx_framebuffer *buffer = get_main_buffer_blur(pass, &blur_options);

	// Update the optimized blur buffer if invalid
	fx_framebuffer_get_or_create_custom(renderer, fx_options->output,
			&pass->fx_effect_framebuffers->optimized_blur_buffer);

	// Render the newly blurred content into the blur_buffer
	fx_renderer_read_to_buffer(pass, &fake_damage,
			pass->fx_effect_framebuffers->optimized_blur_buffer, buffer);

	pixman_region32_fini(&fake_damage);

	pass->fx_effect_framebuffers->blur_buffer_dirty = false;
}

void fx_renderer_read_to_buffer(struct fx_gles_render_pass *pass,
		pixman_region32_t *region, struct fx_framebuffer *dst_buffer,
		struct fx_framebuffer *src_buffer) {
	if (!pixman_region32_not_empty(region)) {
		return;
	}

	struct wlr_texture *src_tex =
		fx_texture_from_buffer(&pass->buffer->renderer->wlr_renderer, src_buffer->buffer);
	if (src_tex == NULL) {
		return;
	}

	// Draw onto the dst_buffer
	fx_framebuffer_bind(dst_buffer);
	wlr_render_pass_add_texture(&pass->base, &(struct wlr_render_texture_options) {
		.texture = src_tex,
		.clip = region,
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
		struct fx_render_timer *timer) {
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

	struct fx_gles_render_pass *pass = calloc(1, sizeof(*pass));
	if (pass == NULL) {
		return NULL;
	}

	wlr_render_pass_init(&pass->base, &render_pass_impl);
	wlr_buffer_lock(wlr_buffer);
	pass->buffer = buffer;
	pass->timer = timer;

	matrix_projection(pass->projection_matrix, wlr_buffer->width, wlr_buffer->height,
		WL_OUTPUT_TRANSFORM_FLIPPED_180);

	push_fx_debug(renderer);
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);

	glViewport(0, 0, wlr_buffer->width, wlr_buffer->height);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_SCISSOR_TEST);
	pop_fx_debug(renderer);

	return pass;
}

struct fx_gles_render_pass *fx_renderer_begin_buffer_pass(
		struct wlr_renderer *wlr_renderer, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output, const struct wlr_buffer_pass_options *options) {
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	renderer->basic_renderer = false;
	if (!wlr_egl_make_current(renderer->egl)) {
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

	// Update the buffers if needed
	struct fx_effect_framebuffers *fbos = NULL;
	// For per output framebuffers
	if (output) {
		fbos = fx_effect_framebuffers_try_get(output);
		fx_framebuffer_get_or_create_custom(renderer, output, &fbos->blur_saved_pixels_buffer);
		fx_framebuffer_get_or_create_custom(renderer, output, &fbos->effects_buffer);
		fx_framebuffer_get_or_create_custom(renderer, output, &fbos->effects_buffer_swapped);
	}

	pixman_region32_init(&renderer->blur_padding_region);

	struct fx_gles_render_pass *pass = begin_buffer_pass(buffer, timer);
	if (!pass) {
		return NULL;
	}
	pass->fx_effect_framebuffers = fbos;
	return pass;
}
