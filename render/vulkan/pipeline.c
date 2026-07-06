#include "render/vulkan/pipeline.h"
#include "render/vulkan/shaders.h"

void vk_pipelines_init(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipelines *out_pipelines) {
	*out_pipelines = (struct vk_pipelines) {0};

	create_vk_quad_pipelines(renderer, setup, &out_pipelines->quad);
	// TODO: More shader pipelines
}

void vk_pipelines_destroy(struct vk_renderer *renderer, struct vk_pipelines *pipelines) {
	delete_vk_quad_pipelines(renderer, pipelines->quad);
	// TODO: More shader pipelines
}
