#include <assert.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include <wayland-util.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/vulkan.h>
#include <wlr/util/log.h>

#include "render/fx_renderer.h"
#include "render/vulkan/pipeline.h"
#include "render/vulkan/shaders.h"
#include "render/vulkan/util.h"
#include "render/vulkan/vulkan.h"
// #include "render/vulkan/shaders.h"

static inline void free_shaders(struct vk_renderer *vk_renderer) {
	vk_shader_info_delete(vk_renderer, &vk_renderer->shader_info.quad);

	// TODO:
	// push_fx_debug(vk_renderer);
	// delete_quad_programs(&vk_renderer->shaders.quad);
	// glDeleteProgram(vk_renderer->shaders.quad_grad.program);
	// glDeleteProgram(vk_renderer->shaders.quad_grad_round.program);
	// delete_tex_programs(&vk_renderer->shaders.tex_rgba);
	// delete_tex_programs(&vk_renderer->shaders.tex_rgbx);
	// delete_tex_programs(&vk_renderer->shaders.tex_ext);
	// glDeleteProgram(vk_renderer->shaders.box_shadow.program);
	// glDeleteProgram(vk_renderer->shaders.blur1.program);
	// glDeleteProgram(vk_renderer->shaders.blur2.program);
	// glDeleteProgram(vk_renderer->shaders.blur_effects.program);
	// pop_fx_debug(vk_renderer);
}

static bool link_shaders(struct vk_renderer *renderer) {
	VkResult res;

	// Vertex shader
	if ((res = create_vk_vert_module(renderer)) != VK_SUCCESS) {
		wlr_vk_error("Failed to create vertex shader module", res);
		return false;
	}

	// quad fragment shader

	if (!vk_shader_info_create_quad(renderer)) {
		wlr_log(WLR_ERROR, "Could not link quad shaders");
		goto error;
	}

	// if (!link_quad_programs(&vk_renderer->shaders.quad)) {
	// 	wlr_log(WLR_ERROR, "Could not link quad shaders");
	// 	goto error;
	// }
	//
	// // quad fragment shader with gradients
	// if (!link_quad_grad_program(&vk_renderer->shaders.quad_grad, 16)) {
	// 	wlr_log(WLR_ERROR, "Could not link quad grad shader");
	// 	goto error;
	// }
	//
	// if (!link_quad_grad_round_program(&vk_renderer->shaders.quad_grad_round, 16)) {
	// 	wlr_log(WLR_ERROR, "Could not link quad grad round shader");
	// 	goto error;
	// }
	//
	// // tex shaders
	// if (!link_tex_programs(&vk_renderer->shaders.tex_rgba,
	// 			SHADER_SOURCE_TEXTURE_RGBA)) {
	// 	wlr_log(WLR_ERROR, "Could not link tex_RGBA shaders");
	// 	goto error;
	// }
	// if (!link_tex_programs(&vk_renderer->shaders.tex_rgbx,
	// 			SHADER_SOURCE_TEXTURE_RGBX)) {
	// 	wlr_log(WLR_ERROR, "Could not link tex_RGBX shaders");
	// 	goto error;
	// }
	// if (!link_tex_programs(&vk_renderer->shaders.tex_ext,
	// 			SHADER_SOURCE_TEXTURE_EXTERNAL)) {
	// 	wlr_log(WLR_ERROR, "Could not link tex_EXTERNAL shaders");
	// 	goto error;
	// }
	//
	// // box shadow shader
	// if (!link_box_shadow_program(&vk_renderer->shaders.box_shadow)) {
	// 	wlr_log(WLR_ERROR, "Could not link box shadow shader");
	// 	goto error;
	// }
	//
	// // Blur shaders
	// if (!link_blur1_program(&vk_renderer->shaders.blur1)) {
	// 	wlr_log(WLR_ERROR, "Could not link blur1 shader");
	// 	goto error;
	// }
	// if (!link_blur2_program(&vk_renderer->shaders.blur2)) {
	// 	wlr_log(WLR_ERROR, "Could not link blur2 shader");
	// 	goto error;
	// }
	// if (!link_blur_effects_program(&vk_renderer->shaders.blur_effects)) {
	// 	wlr_log(WLR_ERROR, "Could not link blur_effects shader");
	// 	goto error;
	// }

	return true;

error:
	free_shaders(renderer);
	return false;
}

static void offscreen_buffers_destroy(struct fx_offscreen_buffers *fx_offscreen_buffers) {
	struct vk_offscreen_buffers *vk_offscreen_buffers = vk_get_offscreen_buffers(fx_offscreen_buffers);
	// Make sure to free the buffers
	if (vk_offscreen_buffers->optimized_blur_buffer != NULL) {
		wlr_buffer_drop(vk_offscreen_buffers->optimized_blur_buffer->wlr_buffer);
		vk_offscreen_buffers->optimized_blur_buffer = NULL;
	}
	if (vk_offscreen_buffers->optimized_no_blur_buffer != NULL) {
		wlr_buffer_drop(vk_offscreen_buffers->optimized_no_blur_buffer->wlr_buffer);
		vk_offscreen_buffers->optimized_no_blur_buffer = NULL;
	}
	if (vk_offscreen_buffers->blur_saved_pixels_buffer != NULL) {
		wlr_buffer_drop(vk_offscreen_buffers->blur_saved_pixels_buffer->wlr_buffer);
		vk_offscreen_buffers->blur_saved_pixels_buffer = NULL;
	}
	if (vk_offscreen_buffers->effects_buffer != NULL) {
		wlr_buffer_drop(vk_offscreen_buffers->effects_buffer->wlr_buffer);
		vk_offscreen_buffers->effects_buffer = NULL;
	}
	if (vk_offscreen_buffers->effects_buffer_swapped != NULL) {
		wlr_buffer_drop(vk_offscreen_buffers->effects_buffer_swapped->wlr_buffer);
		vk_offscreen_buffers->effects_buffer_swapped = NULL;
	}

	free(vk_offscreen_buffers);
}

static const struct fx_offscreen_buffers_impl offscreen_buffers_impl = {
	.destroy = offscreen_buffers_destroy,
};

static struct fx_offscreen_buffers *offscreen_buffers_allocate(
		struct fx_renderer *fx_renderer, struct wlr_output *output) {
	struct vk_offscreen_buffers *vk_offscreen_buffers = calloc(1, sizeof(*vk_offscreen_buffers));
	if (vk_offscreen_buffers == NULL) {
		wlr_log_errno(WLR_ERROR, "vk_offscreen_buffers allocation failed");
		return NULL;
	}

	fx_offscreen_buffers_init(&vk_offscreen_buffers->fx_offscreen_buffers,
			&offscreen_buffers_impl, fx_renderer, output);
	return &vk_offscreen_buffers->fx_offscreen_buffers;
}

static void renderer_destroy(struct fx_renderer *fx_renderer) {
	struct vk_renderer *vk_renderer = vk_get_renderer(fx_renderer);

	struct vk_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &vk_renderer->buffers, link) {
		vk_buffer_destroy(buffer);
	}

	struct vk_render_setup *render_setup, *render_setup_tmp;
	wl_list_for_each_safe(render_setup, render_setup_tmp, &vk_renderer->render_setups, link) {
		vk_render_setup_destroy(vk_renderer, render_setup);
	}

	free_shaders(vk_renderer);

	free(vk_renderer);
}

static const struct fx_renderer_impl renderer_impl = {
	.offscreen_buffers_allocate = offscreen_buffers_allocate,
	.render_pass_allocate = vk_render_pass_init,
	.renderer_destroy = renderer_destroy,

#ifdef TRACY_ENABLE
	.tracy = &vk_renderer_tracy_impl,
#endif
};

struct fx_renderer *vk_renderer_create(struct wlr_renderer *wlr_renderer) {
	struct vk_renderer *vk_renderer = calloc(1, sizeof(*vk_renderer));
	if (vk_renderer == NULL) {
		return NULL;
	}

	wl_list_init(&vk_renderer->buffers);

	vk_renderer->device = wlr_vk_renderer_get_device(wlr_renderer);
	vk_renderer->physical_device = wlr_vk_renderer_get_physical_device(wlr_renderer);
	uint32_t queue_family = wlr_vk_renderer_get_queue_family(wlr_renderer);
	vkGetDeviceQueue(vk_renderer->device, queue_family, 0, &vk_renderer->queue);

	wl_list_init(&vk_renderer->render_setups);

	// Link all shaders
	if (!link_shaders(vk_renderer)) {
		goto shader_error;
	}

	fx_renderer_init(&vk_renderer->fx_renderer, &renderer_impl, wlr_renderer);
	wlr_log(WLR_INFO, "Vulkan FX RENDERER: Shaders Initialized Successfully");
	return &vk_renderer->fx_renderer;

shader_error:
	free(vk_renderer);
	return NULL;
}

static bool fx_renderer_is_vk(const struct fx_renderer *fx_renderer) {
	return fx_renderer->impl == &renderer_impl;
}

static bool fx_offscreen_buffers_is_vk(const struct fx_offscreen_buffers *offscreen_buffers) {
	return offscreen_buffers->impl == &offscreen_buffers_impl;
}

struct vk_renderer *vk_get_renderer(struct fx_renderer *fx_renderer) {
	assert(fx_renderer_is_vk(fx_renderer));
	struct vk_renderer *renderer = wl_container_of(fx_renderer, renderer, fx_renderer);
	return renderer;
}

struct vk_offscreen_buffers *vk_get_offscreen_buffers(
		struct fx_offscreen_buffers *offscreen_buffers) {
	assert(fx_renderer_is_vk(offscreen_buffers->fx_renderer));
	assert(fx_offscreen_buffers_is_vk(offscreen_buffers));
	struct vk_offscreen_buffers *offscreen
		= wl_container_of(offscreen_buffers, offscreen, fx_offscreen_buffers);
	return offscreen;
}

struct vk_render_pass *vk_get_render_pass(struct fx_render_pass *fx_render_pass) {
	assert(fx_renderer_is_vk(fx_render_pass->fx_renderer));
	struct vk_render_pass *pass = wl_container_of(fx_render_pass, pass, fx_render_pass);
	return pass;
}

struct vk_render_setup *vk_render_setup_find_or_create(struct vk_renderer *renderer,
		struct wlr_render_pass *render_pass) {
	const VkRenderPass vk_pass = wlr_vk_render_pass_get_render_pass(render_pass);

	struct vk_render_setup *setup;
	wl_list_for_each(setup, &renderer->render_setups, link) {
		if (setup->render_pass == vk_pass) {
			return setup;
		}
	}

	setup = calloc(1, sizeof(*setup));
	setup->render_pass = vk_pass;

	wl_list_init(&setup->pipelines);
	vk_pipelines_init(renderer, setup, &setup->vk_pipelines);

	wl_list_insert(&renderer->render_setups, &setup->link);
	return setup;
}

void vk_render_setup_destroy(struct vk_renderer *renderer, struct vk_render_setup *render_setup) {
	vk_pipelines_destroy(renderer, &render_setup->vk_pipelines);

	// Fallback
	struct vk_pipeline *pipeline, *pipeline_tmp;
	wl_list_for_each_safe(pipeline, pipeline_tmp, &render_setup->pipelines, link) {
		vkDestroyPipeline(renderer->device, pipeline->pipeline, NULL);
		wl_list_remove(&pipeline->link);
	}

	wl_list_remove(&render_setup->link);

	free(render_setup);
}
