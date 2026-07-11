#include <assert.h>
#include <math.h>
#include <pixman.h>
#include <stdlib.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/interface.h>
#include <wlr/render/vulkan.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include <stdio.h>

#include "render/fx_renderer.h"
#include "render/vulkan/shaders.h"
#include "render/vulkan/vulkan.h"
// #include "render/tracy.h"
#include "scenefx/render/pass.h"
// #include "scenefx/types/fx/blur_data.h"
#include "util/matrix.h"

///
/// wlroots functions
///

// TODO: Fix de-sync with upstream pass. Upstream renderer can change the bound
// pipeline without us knowing
static void bind_pipeline(struct vk_render_pass *pass, struct vk_pipeline *pipeline) {
	if (pipeline->pipeline == pass->bound_pipeline) {
		return;
	}

	vkCmdBindPipeline(pass->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
	pass->bound_pipeline = pipeline->pipeline;
}

static void get_clip_region(struct vk_render_pass *pass,
		const pixman_region32_t *in, pixman_region32_t *out) {
	if (in != NULL) {
		pixman_region32_init(out);
		pixman_region32_copy(out, in);
	} else {
		struct wlr_buffer *buffer = pass->render_buffer->wlr_buffer;
		pixman_region32_init_rect(out, 0, 0, buffer->width, buffer->height);
	}
}

static void convert_pixman_box_to_vk_rect(const pixman_box32_t *box, VkRect2D *rect) {
	*rect = (VkRect2D){
		.offset = { .x = box->x1, .y = box->y1 },
		.extent = { .width = box->x2 - box->x1, .height = box->y2 - box->y1 },
	};
}

static float color_to_linear(float non_linear) {
	return pow(non_linear, 2.2);
}

static float color_to_linear_premult(float non_linear, float alpha) {
	return (alpha == 0) ? 0 : color_to_linear(non_linear / alpha) * alpha;
}

static void encode_proj_matrix(const float mat3[9], float mat4[4][4]) {
	float result[4][4] = {
		{ mat3[0], mat3[1], 0, mat3[2] },
		{ mat3[3], mat3[4], 0, mat3[5] },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 },
	};

	memcpy(mat4, result, sizeof(result));
}

// static void encode_color_matrix(const float mat3[9], float mat4[4][4]) {
// 	float result[4][4] = {
// 		{ mat3[0], mat3[1], mat3[2], 0 },
// 		{ mat3[3], mat3[4], mat3[5], 0 },
// 		{ mat3[6], mat3[7], mat3[8], 0 },
// 		{ 0, 0, 0, 0 },
// 	};
//
// 	memcpy(mat4, result, sizeof(result));
// }

///
/// FX pass functions
///

static void render_pass_mark_box_updated(struct vk_render_pass *pass,
		const struct wlr_box *box) {
	pixman_region32_t region;
	pixman_region32_init_rect(&region, box->x, box->y, box->width, box->height);

	wlr_vk_render_pass_mark_updated(pass->fx_render_pass.render_pass, &region);

	pixman_region32_fini(&region);
}

static bool apply_clip_region(pixman_region32_t *clip_region,
		const struct wlr_box *clipped_region_box, const struct fx_corner_fradii *corners) {
	if (!wlr_box_empty(clipped_region_box)) {
		float top = fmax(corners->top_left, corners->top_right);
		float bottom = fmax(corners->bottom_left, corners->bottom_right);
		float left = fmax(corners->top_left, corners->bottom_left);
		float right = fmax(corners->top_right, corners->bottom_right);

		pixman_region32_t user_clip_region;
		pixman_region32_init_rect(
			&user_clip_region,
			clipped_region_box->x + (left * 0.3),
			clipped_region_box->y + (top * 0.3),
			fmax(clipped_region_box->width - (left + right) * 0.3, 0),
			fmax(clipped_region_box->height - (top + bottom) * 0.3, 0)
		);
		pixman_region32_subtract(clip_region, clip_region, &user_clip_region);
		pixman_region32_fini(&user_clip_region);
		return true;
	}

	return false;
}

///
/// FX Render Pass implementation
///

static void vk_render_pass_read_to_buffer(struct fx_render_pass *fx_pass,
		const pixman_region32_t *_region, struct wlr_buffer *dst_buffer,
		struct wlr_buffer *src_buffer) {
	// TODO:
}

static void vk_render_pass_save_blur_region(struct fx_render_pass *fx_pass) {
	// TODO:
	// struct vk_render_pass *pass = vk_get_render_pass(fx_pass);
	// vk_render_pass_read_to_buffer(fx_pass, &fx_pass->blur_padding_region,
	// 		pass->vk_offscreen_buffers->blur_saved_pixels_buffer->wlr_buffer,
	// 		pass->render_buffer->wlr_buffer);
}

static void vk_render_pass_apply_saved_blur_region(struct fx_render_pass *fx_pass) {
	// TODO:
	// struct vk_render_pass *pass = vk_get_render_pass(fx_pass);
	// vk_render_pass_read_to_buffer(fx_pass, &fx_pass->blur_padding_region,
	// 		pass->render_buffer->wlr_buffer,
	// 		pass->vk_offscreen_buffers->blur_saved_pixels_buffer->wlr_buffer);
}

static void vk_render_pass_destroy(struct fx_render_pass *fx_pass) {
	struct vk_render_pass *vk_render_pass = vk_get_render_pass(fx_pass);
	free(vk_render_pass);
}

static void vk_render_pass_add_texture(struct fx_render_pass *fx_pass,
		const struct fx_render_texture_options *fx_options) {
	// TODO: Fallback to the regular wlr render path
	wlr_render_pass_add_texture(fx_pass->render_pass, &fx_options->base);
}

static void vk_render_pass_add_rect(struct fx_render_pass *fx_pass,
		const struct fx_render_rect_options *fx_options) {
	const struct wlr_render_rect_options *options = &fx_options->base;
	struct vk_render_pass *pass = vk_get_render_pass(fx_pass);
	struct vk_renderer *vk_renderer = pass->vk_renderer;
	VkCommandBuffer cb = pass->command_buffer;

	// Input color values are given in sRGB space, shader expects
	// them in linear space. The shader does all computation in linear
	// space and expects in inputs in linear space since it outputs
	// colors in linear space as well (and vulkan then automatically
	// does the conversion for out sRGB render targets).
	float linear_color[] = {
		color_to_linear_premult(options->color.r, options->color.a),
		color_to_linear_premult(options->color.g, options->color.a),
		color_to_linear_premult(options->color.b, options->color.a),
		options->color.a, // no conversion for alpha
	};

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	// Record regions possibly updated for use in second subpass
	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->render_buffer->wlr_buffer, &box);

	const bool should_round_corners = !fx_corner_fradii_is_empty(&fx_options->corners);
	const bool should_clip = clipped_fregion_is_valid(&fx_options->clipped_region);
	// Gather the effects and pick the relevant shader
	enum fx_quad_shader_effects effects = SHADER_QUAD_EFFECT_NONE;
	if (should_round_corners) {
		effects |= SHADER_QUAD_EFFECT_ROUND_CORNERS;
	}
	if (should_clip) {
		effects |= SHADER_QUAD_EFFECT_CLIPPING;
	}

	float proj[9], matrix[9];
	wlr_matrix_identity(proj);
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, proj);
	wlr_matrix_multiply(matrix, pass->projection, matrix);

	VkPipelineLayout layout = vk_renderer->shader_info.quad.pipeline_layout;
	struct vk_pipeline *pipeline = get_vk_quad_pipeline(pass->render_setup->vk_pipelines.quad, effects);
	if (pipeline == NULL) {
		return;
	}

	const struct wlr_box *clipped_region_box = &fx_options->clipped_region.area;
	const struct fx_corner_fradii *clipped_region_corners = &fx_options->clipped_region.corners;
	pixman_region32_t clip_region;
	if (options->clip) {
		pixman_region32_init(&clip_region);
		pixman_region32_copy(&clip_region, options->clip);
	} else {
		pixman_region32_init_rect(&clip_region, box.x, box.y, box.width, box.height);
	}
	apply_clip_region(&clip_region, clipped_region_box, clipped_region_corners);

	struct vk_vert_pcr_data vert_pcr_data = {
		.uv_off = { 0, 0 },
		.uv_size = { 1, 1 },
	};
	encode_proj_matrix(matrix, vert_pcr_data.mat4);
	struct vk_frag_quad_pcr_data quad_pcr_data = {
		.color = {linear_color[0], linear_color[1], linear_color[2], linear_color[3]},
		.corner_rounding = {
			.size = {box.width, box.height},
			.position = {box.x, box.y},
			.radius = {
				.top_left = fx_options->corners.top_left,
				.top_right = fx_options->corners.top_right,
				.bottom_left = fx_options->corners.bottom_left,
				.bottom_right = fx_options->corners.bottom_right,
			}
		},
		.clipping = {
			.size = {clipped_region_box->width, clipped_region_box->height},
			.position = {clipped_region_box->x, clipped_region_box->y},
			.radius = {
				.top_left = clipped_region_corners->top_left,
				.top_right = clipped_region_corners->top_right,
				.bottom_left = clipped_region_corners->bottom_left,
				.bottom_right = clipped_region_corners->bottom_right,
			}
		},
	};

	bind_pipeline(pass, pipeline);
	vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(vert_pcr_data), &vert_pcr_data);
	vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
			sizeof(vert_pcr_data),
			sizeof(quad_pcr_data), &quad_pcr_data);

	for (int i = 0; i < clip_rects_len; i++) {
		VkRect2D rect;
		convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
		vkCmdSetScissor(cb, 0, 1, &rect);
		vkCmdDraw(cb, 4, 1, 0, 0);
	}

	// TODO: Performance penalty for constantly setting pipeline?
	wlr_vk_render_pass_reset_pipeline(fx_pass->render_pass);
	pass->bound_pipeline = VK_NULL_HANDLE;

	pixman_region32_fini(&clip_region);
	pixman_region32_fini(&clip);
}

static void vk_render_pass_add_rect_grad(struct fx_render_pass *fx_pass,
		const struct fx_render_rect_grad_options *fx_options) {
	// TODO:
}

static void vk_render_pass_add_rounded_rect_grad(struct fx_render_pass *fx_pass,
		const struct fx_render_rounded_rect_grad_options *fx_options) {
	// TODO:
}

static void vk_render_pass_add_box_shadow(struct fx_render_pass *fx_pass,
		const struct fx_render_box_shadow_options *options) {
	// TODO:
}

// TODO:
// // Renders the blur for each damaged rect and swaps the buffer
// static void render_blur_segments(struct vk_render_pass *pass,
// 		// TODO: make fx_options immutable in the future
// 		struct fx_render_blur_pass_options *fx_options, struct blur_shader* shader) {
// }
//
// static void render_blur_effects(struct vk_render_pass *pass,
// 		// TODO: make fx_options immutable in the future
// 		struct fx_render_blur_pass_options *fx_options) {
// }
//
// // Blurs the fx_options current_buffer content and returns the blurred framebuffer.
// // Returns NULL when the blur parameters reach 0.
// static struct vk_buffer *get_main_buffer_blur(struct vk_render_pass *pass,
// 		// TODO: make fx_options immutable in the future
// 		struct fx_render_blur_pass_options *fx_options) {
// }

static void vk_render_pass_add_blur(struct fx_render_pass *fx_pass,
		const struct fx_render_blur_pass_options *fx_options) {
	// TODO:
}

static bool vk_render_pass_add_optimized_blur(struct fx_render_pass *fx_pass,
		const struct fx_render_blur_pass_options *fx_options) {
	// TODO:
	return false;
}

static const struct fx_render_pass_impl render_pass_impl = {
	.destroy = vk_render_pass_destroy,
	.add_texture = vk_render_pass_add_texture,
	.add_rect = vk_render_pass_add_rect,
	.add_rect_grad = vk_render_pass_add_rect_grad,
	.add_rounded_rect_grad = vk_render_pass_add_rounded_rect_grad,
	.add_box_shadow = vk_render_pass_add_box_shadow,
	.add_blur = vk_render_pass_add_blur,
	.add_optimized_blur = vk_render_pass_add_optimized_blur,
	.read_to_buffer = vk_render_pass_read_to_buffer,
	.save_blur_region = vk_render_pass_save_blur_region,
	.apply_saved_blur_region = vk_render_pass_apply_saved_blur_region,
};

// TODO: Release necessary resources
static void handle_release_resources(void *user_data) {
	// struct vk_render_pass *pass = user_data;
}

// TODO:
struct fx_render_pass *vk_render_pass_init(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output) {
	if (fx_renderer == NULL || render_pass == NULL || wlr_buffer == NULL
			|| output == NULL) {
		wlr_log(WLR_DEBUG, "Could not create vk_render_pass, NULL parameters");
		return NULL;
	}

	struct vk_render_pass *pass = calloc(1, sizeof(*pass));
	if (pass == NULL) {
		wlr_log(WLR_ERROR, "Failed to create fx_gles_render_pass");
		return NULL;
	}

	fx_render_pass_init(&pass->fx_render_pass, &render_pass_impl, fx_renderer, render_pass);

	pass->vk_renderer = vk_get_renderer(fx_renderer);
	pass->render_buffer = vk_buffer_get_or_create(fx_renderer, wlr_buffer, true);
	if (pass->render_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to get/create vk main buffer");
		goto buffer_get_fail;
	}

	pass->command_buffer = wlr_vk_render_pass_get_command_buffer(render_pass);
	pass->render_setup = vk_render_setup_find_or_create(pass->vk_renderer, render_pass);
	assert(pass->render_setup);

	pass->bound_pipeline = VK_NULL_HANDLE;

	// Save the same matrix projection that vk_render_pass uses
	matrix_projection(pass->projection, wlr_buffer->width, wlr_buffer->height,
		WL_OUTPUT_TRANSFORM_FLIPPED_180);

	// For per output framebuffers
	struct fx_offscreen_buffers *fx_offscreen_buffers = fx_offscreen_buffers_try_get(fx_renderer, output);
	pass->vk_offscreen_buffers = vk_get_offscreen_buffers(fx_offscreen_buffers);
	if (pass->vk_offscreen_buffers == NULL) {
		wlr_log(WLR_ERROR, "Failed to get/create effect framebuffers for output: %s",
				output->name);
		goto offscreen_buffers_get_create_fail;
	}

	// Update the buffers if needed
	const int width = pass->render_buffer->wlr_buffer->width;
	const int height = pass->render_buffer->wlr_buffer->height;
	bool failed = false;
	vk_buffer_get_or_allocate(fx_renderer, output->allocator, width, height, false,
			&pass->vk_offscreen_buffers->blur_saved_pixels_buffer, &failed);
	vk_buffer_get_or_allocate(fx_renderer, output->allocator, width, height, true,
			&pass->vk_offscreen_buffers->effects_buffer, &failed);
	vk_buffer_get_or_allocate(fx_renderer, output->allocator, width, height, true,
			&pass->vk_offscreen_buffers->effects_buffer_swapped, &failed);
	vk_buffer_get_or_allocate(fx_renderer, output->allocator, width, height, false,
			&pass->vk_offscreen_buffers->optimized_blur_buffer, &failed);
	vk_buffer_get_or_allocate(fx_renderer, output->allocator, width, height, false,
			&pass->vk_offscreen_buffers->optimized_no_blur_buffer, &failed);

	// Bind back to the default buffer
	// glBindFramebuffer(GL_FRAMEBUFFER, pass->vk_buffer->fbo);

	if (failed) {
		wlr_log(WLR_ERROR, "Failed to create effect framebuffers");
		goto offscreen_buffer_allocate_fail;
	}

	// TODO:
	wlr_vk_render_pass_set_resources_callback(render_pass, handle_release_resources, pass);

	return &pass->fx_render_pass;

offscreen_buffer_allocate_fail:
	fx_offscreen_buffers_destroy(&pass->vk_offscreen_buffers->fx_offscreen_buffers);
offscreen_buffers_get_create_fail:
	vk_buffer_destroy(pass->render_buffer);
	pass->render_buffer = NULL;
buffer_get_fail:
	free(pass);
	return NULL;
}
