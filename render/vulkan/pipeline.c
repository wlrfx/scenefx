#include "render/vulkan/pipeline.h"
#include "render/vulkan/shaders.h"

void vk_pipelines_init(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipelines *out_pipelines) {
	*out_pipelines = (struct vk_pipelines) {0};

	create_vk_quad_pipelines(renderer, setup, &out_pipelines->quad);
	create_vk_blur_pipelines(renderer, setup, &out_pipelines->blur1,
			&out_pipelines->blur2, &out_pipelines->blur_effects);
	// TODO: More shader pipelines
}

void vk_pipelines_destroy(struct vk_renderer *renderer, struct vk_pipelines *pipelines) {
	delete_vk_quad_pipelines(renderer, pipelines->quad);
	delete_vk_blur_pipelines(renderer, pipelines->blur1);
	delete_vk_blur_pipelines(renderer, pipelines->blur2);
	delete_vk_blur_pipelines(renderer, pipelines->blur_effects);
	// TODO: More shader pipelines
}
