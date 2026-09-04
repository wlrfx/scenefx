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

///
/// Blur
///

// Field order matches the shader: the vec2 comes first so it keeps its 8 byte
// alignment without needing explicit padding.
struct vk_frag_blur_pcr_data {
	float halfpixel[2];
	float radius;
};

struct vk_frag_blur_effects_pcr_data {
	float brightness;
	float contrast;
	float saturation;
	float noise;
};

struct vk_pipeline_blur {
	struct vk_pipeline pipeline;
	// Layout of the shader this pipeline was created from. The blur layouts
	// are currently identical, so binding through any of them happens to
	// work, but that is not something to rely on.
	VkPipelineLayout pipeline_layout;
};

// Sampling path for the blur passes: a single combined image sampler at
// binding 0 using our own immutable LINEAR/CLAMP_TO_EDGE sampler.
// NOT interchangeable with wlroots' texture descriptor sets - its layout
// declares a different immutable sampler, and Vulkan only considers layouts
// compatible when identically defined.
VkDescriptorSetLayout vk_get_tex_ds_layout(struct vk_renderer *renderer);
VkDescriptorSet vk_tex_ds_create(struct vk_renderer *renderer, VkImageView view);
void vk_tex_ds_destroy(struct vk_renderer *renderer, VkDescriptorSet ds);
void vk_tex_ds_layout_destroy(struct vk_renderer *renderer);

bool vk_shader_info_create_blur1(struct vk_renderer *renderer);
bool vk_shader_info_create_blur2(struct vk_renderer *renderer);
bool vk_shader_info_create_blur_effects(struct vk_renderer *renderer);

bool create_vk_blur_pipelines(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipeline_blur **out_blur1, struct vk_pipeline_blur **out_blur2,
		struct vk_pipeline_blur **out_blur_effects);
void delete_vk_blur_pipelines(struct vk_renderer *renderer, struct vk_pipeline_blur *blur);

#endif // !_RENDERER_VULKAN_SHADERS_H
