#ifndef _RENDERER_VULKAN_PIPELINE_H
#define _RENDERER_VULKAN_PIPELINE_H

#include <vulkan/vulkan_core.h>
#include <wayland-util.h>

struct vk_renderer;
struct vk_render_setup;

struct vk_pipeline {
	struct wl_list link; // struct vk_render_setup.pipelines
	VkPipeline pipeline;
};

struct vk_pipelines {
	struct vk_pipeline_quad *quad;
	struct vk_pipeline_blur *blur1;
	struct vk_pipeline_blur *blur2;
	struct vk_pipeline_blur *blur_effects;
};

void vk_pipelines_init(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipelines *out_pipelines);
void vk_pipelines_destroy(struct vk_renderer *renderer, struct vk_pipelines *pipelines);

#endif // !_RENDERER_VULKAN_PIPELINE_H
