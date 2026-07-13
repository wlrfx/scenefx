#ifndef _RENDERER_VULKAN_SHADERS_H
#define _RENDERER_VULKAN_SHADERS_H

#include <vulkan/vulkan_core.h>

#include "render/shaders.h"
#include "render/vulkan/pipeline.h"

struct vk_render_pass;
struct vk_renderer;

struct vk_shader_rounding_data {
	float size[2];
	float position[2];
	struct {
		float top_left;
		float top_right;
		float bottom_left;
		float bottom_right;
	} radius;
};

///
/// Shader info
///

struct vk_shader_info {
	VkShaderModule shader_module;
	VkPipelineLayout pipeline_layout;
};

void vk_shader_info_delete(struct vk_renderer *renderer, struct vk_shader_info *shader_info);

///
/// Vert
///

struct vk_vert_pcr_data {
	float mat4[4][4];
	float uv_off[2];
	float uv_size[2];
};

VkResult create_vk_vert_module(struct vk_renderer *renderer);

///
/// Quad
///

struct vk_frag_quad_pcr_data {
	float color[4];
	struct vk_shader_rounding_data corner_rounding;
	struct vk_shader_rounding_data clipping;
};

struct vk_pipeline_quad {
	// 4 different combinations of effects
	struct vk_pipeline variants[SHADER_QUAD_EFFECT_LAST];
};

bool create_vk_quad_pipelines(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipeline_quad **out_quad);
void delete_vk_quad_pipelines(struct vk_renderer *renderer, struct vk_pipeline_quad *quad);
struct vk_pipeline *get_vk_quad_pipeline(struct vk_pipeline_quad *quad,
		enum fx_quad_shader_effects effects);

bool vk_shader_info_create_quad(struct vk_renderer *renderer);

#endif // !_RENDERER_VULKAN_SHADERS_H
