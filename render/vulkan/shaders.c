#include <assert.h>
#include <scenefx/types/fx/clipped_region.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include <wayland-util.h>
#include <wlr/render/vulkan.h>
#include <wlr/util/log.h>

#include "render/vulkan/shaders.h"
#include "render/vulkan/vulkan.h"

// shaders
#include "common.vert.h"
#include "quad.frag.h"
#include "blur1.frag.h"
#include "blur2.frag.h"
#include "blur_effects.frag.h"

void vk_shader_info_delete(struct vk_renderer *renderer, struct vk_shader_info *shader_info) {
	vkDestroyShaderModule(renderer->device, shader_info->shader_module, NULL);
	vkDestroyPipelineLayout(renderer->device, shader_info->pipeline_layout, NULL);
}

///
/// Vert
///

VkResult create_vk_vert_module(struct vk_renderer *renderer) {
	VkShaderModuleCreateInfo shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(common_vert_data),
		.pCode = common_vert_data,
	};
	return vkCreateShaderModule(renderer->device, &shader_info, NULL, &renderer->shader_info.vert);
}

///
/// Quad
///

static bool create_pipeline_quad(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipeline *variant, const enum fx_quad_shader_effects _effects) {
	const uint32_t effects = _effects;
	struct vk_shader_info *quad = &renderer->shader_info.quad;

	VkResult res;

	// Specialization constant
	VkSpecializationMapEntry spec_entry = {
		.constantID = 0,
		.offset = 0,
		.size = sizeof(effects),
	};
	VkSpecializationInfo specialization = {
		.mapEntryCount = 1,
		.pMapEntries = &spec_entry,
		.dataSize = sizeof(effects),
		.pData = &effects,
	};

	// Create pipeline
	VkPipelineShaderStageCreateInfo stages[] = {
		(VkPipelineShaderStageCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = renderer->shader_info.vert,
			.pName = "main",
		},
		(VkPipelineShaderStageCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = quad->shader_module,
			.pName = "main",
			.pSpecializationInfo = &specialization,
		},
	};

	VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	};

	VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.f,
	};

	VkPipelineColorBlendAttachmentState blend_attachment = {
		.blendEnable = true,
		// we generally work with pre-multiplied alpha
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};

	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkDynamicState dyn_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pDynamicStates = dyn_states,
		.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]),
	};

	VkPipelineVertexInputStateCreateInfo vertex = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};

	VkGraphicsPipelineCreateInfo pinfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.layout = quad->pipeline_layout,
		.renderPass = setup->render_pass,
		.subpass = 0,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,

		.pInputAssemblyState = &assembly,
		.pRasterizationState = &rasterization,
		.pColorBlendState = &blend,
		.pMultisampleState = &multisample,
		.pViewportState = &viewport,
		.pDynamicState = &dynamic,
		.pVertexInputState = &vertex,
	};

	VkPipelineCache cache = VK_NULL_HANDLE;
	res = vkCreateGraphicsPipelines(renderer->device, cache, 1, &pinfo, NULL, &variant->pipeline);
	if (res != VK_SUCCESS) {
		wlr_vk_error("failed to create vulkan pipelines:", res);
		return false;
	}

	wl_list_insert(&setup->pipelines, &variant->link);
	return true;
}

bool create_vk_quad_pipelines(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipeline_quad **out_quad) {
	*out_quad = NULL;
	struct vk_pipeline_quad *quad = calloc(1, sizeof(*quad));

	for (enum fx_quad_shader_effects effects = 0;
			effects < SHADER_QUAD_EFFECT_LAST;
			effects++) {
		struct vk_pipeline *variant = &quad->variants[effects];
		variant->pipeline = VK_NULL_HANDLE;

		if (!create_pipeline_quad(renderer, setup, variant, effects)) {
			wlr_log(WLR_ERROR, "Could not create quad shader pipeline: Effects: \"%d\"",
					effects);
			goto failed;
		}
	}

	*out_quad = quad;
	return true;

failed:
	// Remove all created pipelines
	delete_vk_quad_pipelines(renderer, quad);
	return false;
}

void delete_vk_quad_pipelines(struct vk_renderer *renderer, struct vk_pipeline_quad *quad) {
	for (enum fx_quad_shader_effects effects = 0;
			effects < SHADER_QUAD_EFFECT_LAST;
			effects++) {
		struct vk_pipeline *variant = &quad->variants[effects];
		if (variant->pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(renderer->device, variant->pipeline, NULL);
			wl_list_remove(&variant->link);
		}
	}
}

struct vk_pipeline *get_vk_quad_pipeline(struct vk_pipeline_quad *quad,
		enum fx_quad_shader_effects effects) {
	assert(effects < SHADER_QUAD_EFFECT_LAST);
	return &quad->variants[effects];
}

bool vk_shader_info_create_quad(struct vk_renderer *renderer) {
	struct vk_shader_info *quad = &renderer->shader_info.quad;

	// Initialize the shader module from the compiled SPIR-V code
	VkShaderModuleCreateInfo shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = sizeof(quad_frag_data),
		.pCode = quad_frag_data,
	};
	VkResult res = vkCreateShaderModule(renderer->device, &shader_info, NULL, &quad->shader_module);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Failed to create quad fragment shader module", res);
		return false;
	}

	// Create pipeline layout
	VkPushConstantRange pc_ranges[] = {
		{
			.size = sizeof(struct vk_vert_pcr_data),
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.offset = pc_ranges[0].size,
			.size = sizeof(struct vk_frag_quad_pcr_data),
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	VkPipelineLayoutCreateInfo pl_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = sizeof(pc_ranges) / sizeof(pc_ranges[0]),
		.pPushConstantRanges = pc_ranges,
	};

	res = vkCreatePipelineLayout(renderer->device, &pl_info, NULL, &quad->pipeline_layout);
	if (res != VK_SUCCESS) {
		vkDestroyShaderModule(renderer->device, quad->shader_module, NULL);
		wlr_vk_error("vkCreatePipelineLayout", res);
		return false;
	}

	return true;
}

///
/// Blur
///

// Descriptor sets for the blur passes are allocated from this pool, in this
// layout, and always sample through our own immutable sampler. Note this is
// deliberately NOT compatible with wlroots' texture descriptor sets - see the
// comment on tex_sampler in vulkan.h.
#define VK_TEX_DS_POOL_SIZE 64

VkDescriptorSetLayout vk_get_tex_ds_layout(struct vk_renderer *renderer) {
	if (renderer->shader_info.tex_ds_layout != VK_NULL_HANDLE) {
		return renderer->shader_info.tex_ds_layout;
	}

	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxAnisotropy = 1.f,
		.minLod = 0.f,
		.maxLod = 0.25f,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
	};
	VkResult res = vkCreateSampler(renderer->device, &sampler_info, NULL,
			&renderer->shader_info.tex_sampler);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateSampler (blur)", res);
		return VK_NULL_HANDLE;
	}

	VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.pImmutableSamplers = &renderer->shader_info.tex_sampler,
	};
	VkDescriptorSetLayoutCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	res = vkCreateDescriptorSetLayout(renderer->device, &info, NULL,
			&renderer->shader_info.tex_ds_layout);
	if (res != VK_SUCCESS) {
		vkDestroySampler(renderer->device, renderer->shader_info.tex_sampler, NULL);
		renderer->shader_info.tex_sampler = VK_NULL_HANDLE;
		wlr_vk_error("vkCreateDescriptorSetLayout (tex)", res);
		return VK_NULL_HANDLE;
	}

	VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = VK_TEX_DS_POOL_SIZE,
	};
	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = VK_TEX_DS_POOL_SIZE,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	res = vkCreateDescriptorPool(renderer->device, &pool_info, NULL,
			&renderer->shader_info.tex_ds_pool);
	if (res != VK_SUCCESS) {
		vkDestroyDescriptorSetLayout(renderer->device,
				renderer->shader_info.tex_ds_layout, NULL);
		renderer->shader_info.tex_ds_layout = VK_NULL_HANDLE;
		vkDestroySampler(renderer->device, renderer->shader_info.tex_sampler, NULL);
		renderer->shader_info.tex_sampler = VK_NULL_HANDLE;
		wlr_vk_error("vkCreateDescriptorPool (tex)", res);
		return VK_NULL_HANDLE;
	}

	return renderer->shader_info.tex_ds_layout;
}

VkDescriptorSet vk_tex_ds_create(struct vk_renderer *renderer, VkImageView view) {
	VkDescriptorSetLayout layout = vk_get_tex_ds_layout(renderer);
	if (layout == VK_NULL_HANDLE) {
		return VK_NULL_HANDLE;
	}

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = renderer->shader_info.tex_ds_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};
	VkDescriptorSet ds = VK_NULL_HANDLE;
	VkResult res = vkAllocateDescriptorSets(renderer->device, &alloc_info, &ds);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocateDescriptorSets (tex)", res);
		return VK_NULL_HANDLE;
	}

	VkDescriptorImageInfo image_info = {
		// sampler comes from the layout's immutable sampler
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ds,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
	};
	vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);

	return ds;
}

void vk_tex_ds_destroy(struct vk_renderer *renderer, VkDescriptorSet ds) {
	if (ds == VK_NULL_HANDLE || renderer->shader_info.tex_ds_pool == VK_NULL_HANDLE) {
		return;
	}
	vkFreeDescriptorSets(renderer->device, renderer->shader_info.tex_ds_pool, 1, &ds);
}

void vk_tex_ds_layout_destroy(struct vk_renderer *renderer) {
	if (renderer->shader_info.tex_ds_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(renderer->device, renderer->shader_info.tex_ds_pool, NULL);
		renderer->shader_info.tex_ds_pool = VK_NULL_HANDLE;
	}
	if (renderer->shader_info.tex_ds_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(renderer->device,
				renderer->shader_info.tex_ds_layout, NULL);
		renderer->shader_info.tex_ds_layout = VK_NULL_HANDLE;
	}
	if (renderer->shader_info.tex_sampler != VK_NULL_HANDLE) {
		vkDestroySampler(renderer->device, renderer->shader_info.tex_sampler, NULL);
		renderer->shader_info.tex_sampler = VK_NULL_HANDLE;
	}
}

// Shared by all three blur shaders: one sampler set plus a vertex and a
// fragment push constant range. `frag_pcr_size` is what differs between them.
static bool create_blur_shader_info(struct vk_renderer *renderer,
		struct vk_shader_info *shader, const uint32_t *spv, size_t spv_size,
		size_t frag_pcr_size, const char *name) {
	VkShaderModuleCreateInfo shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = spv_size,
		.pCode = spv,
	};
	VkResult res = vkCreateShaderModule(renderer->device, &shader_info, NULL,
			&shader->shader_module);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Failed to create blur fragment shader module", res);
		wlr_log(WLR_ERROR, "  shader: %s", name);
		return false;
	}

	VkDescriptorSetLayout ds_layout = vk_get_tex_ds_layout(renderer);
	if (ds_layout == VK_NULL_HANDLE) {
		vkDestroyShaderModule(renderer->device, shader->shader_module, NULL);
		return false;
	}

	VkPushConstantRange pc_ranges[] = {
		{
			.size = sizeof(struct vk_vert_pcr_data),
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			// Must not be written as pc_ranges[0].size: reading the array
			// while it is still being initialised is unspecified.
			.offset = sizeof(struct vk_vert_pcr_data),
			.size = frag_pcr_size,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	VkPipelineLayoutCreateInfo pl_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &ds_layout,
		.pushConstantRangeCount = sizeof(pc_ranges) / sizeof(pc_ranges[0]),
		.pPushConstantRanges = pc_ranges,
	};

	res = vkCreatePipelineLayout(renderer->device, &pl_info, NULL, &shader->pipeline_layout);
	if (res != VK_SUCCESS) {
		vkDestroyShaderModule(renderer->device, shader->shader_module, NULL);
		wlr_vk_error("vkCreatePipelineLayout (blur)", res);
		return false;
	}

	return true;
}

bool vk_shader_info_create_blur1(struct vk_renderer *renderer) {
	return create_blur_shader_info(renderer, &renderer->shader_info.blur1,
		blur1_frag_data, sizeof(blur1_frag_data),
		sizeof(struct vk_frag_blur_pcr_data), "blur1");
}

bool vk_shader_info_create_blur2(struct vk_renderer *renderer) {
	return create_blur_shader_info(renderer, &renderer->shader_info.blur2,
		blur2_frag_data, sizeof(blur2_frag_data),
		sizeof(struct vk_frag_blur_pcr_data), "blur2");
}

bool vk_shader_info_create_blur_effects(struct vk_renderer *renderer) {
	return create_blur_shader_info(renderer, &renderer->shader_info.blur_effects,
		blur_effects_frag_data, sizeof(blur_effects_frag_data),
		sizeof(struct vk_frag_blur_effects_pcr_data), "blur_effects");
}

// The blur passes write into offscreen buffers and fully replace their
// contents, so unlike the quad pipeline they disable blending.
static bool create_pipeline_blur(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_shader_info *shader, struct vk_pipeline *variant) {
	VkPipelineShaderStageCreateInfo stages[] = {
		(VkPipelineShaderStageCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = renderer->shader_info.vert,
			.pName = "main",
		},
		(VkPipelineShaderStageCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = shader->shader_module,
			.pName = "main",
		},
	};

	VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	};

	VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.f,
	};

	VkPipelineColorBlendAttachmentState blend_attachment = {
		.blendEnable = false,
		.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};

	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkDynamicState dyn_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pDynamicStates = dyn_states,
		.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]),
	};

	VkPipelineVertexInputStateCreateInfo vertex = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};

	VkGraphicsPipelineCreateInfo pinfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.layout = shader->pipeline_layout,
		.renderPass = setup->render_pass,
		.subpass = 0,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,

		.pInputAssemblyState = &assembly,
		.pRasterizationState = &rasterization,
		.pColorBlendState = &blend,
		.pMultisampleState = &multisample,
		.pViewportState = &viewport,
		.pDynamicState = &dynamic,
		.pVertexInputState = &vertex,
	};

	VkPipelineCache cache = VK_NULL_HANDLE;
	VkResult res = vkCreateGraphicsPipelines(renderer->device, cache, 1, &pinfo, NULL,
			&variant->pipeline);
	if (res != VK_SUCCESS) {
		wlr_vk_error("failed to create blur pipeline:", res);
		return false;
	}

	wl_list_insert(&setup->pipelines, &variant->link);
	return true;
}

static struct vk_pipeline_blur *create_one_blur_pipeline(struct vk_renderer *renderer,
		struct vk_render_setup *setup, struct vk_shader_info *shader) {
	struct vk_pipeline_blur *blur = calloc(1, sizeof(*blur));
	if (blur == NULL) {
		return NULL;
	}
	blur->pipeline.pipeline = VK_NULL_HANDLE;
	if (!create_pipeline_blur(renderer, setup, shader, &blur->pipeline)) {
		free(blur);
		return NULL;
	}
	blur->pipeline_layout = shader->pipeline_layout;
	return blur;
}

bool create_vk_blur_pipelines(struct vk_renderer *renderer, struct vk_render_setup *setup,
		struct vk_pipeline_blur **out_blur1, struct vk_pipeline_blur **out_blur2,
		struct vk_pipeline_blur **out_blur_effects) {
	*out_blur1 = create_one_blur_pipeline(renderer, setup, &renderer->shader_info.blur1);
	*out_blur2 = create_one_blur_pipeline(renderer, setup, &renderer->shader_info.blur2);
	*out_blur_effects = create_one_blur_pipeline(renderer, setup,
			&renderer->shader_info.blur_effects);

	if (*out_blur1 == NULL || *out_blur2 == NULL || *out_blur_effects == NULL) {
		wlr_log(WLR_ERROR, "Could not create blur shader pipelines");
		delete_vk_blur_pipelines(renderer, *out_blur1);
		delete_vk_blur_pipelines(renderer, *out_blur2);
		delete_vk_blur_pipelines(renderer, *out_blur_effects);
		*out_blur1 = *out_blur2 = *out_blur_effects = NULL;
		return false;
	}
	return true;
}

void delete_vk_blur_pipelines(struct vk_renderer *renderer, struct vk_pipeline_blur *blur) {
	if (blur == NULL) {
		return;
	}
	if (blur->pipeline.pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(renderer->device, blur->pipeline.pipeline, NULL);
		wl_list_remove(&blur->pipeline.link);
	}
	free(blur);
}
