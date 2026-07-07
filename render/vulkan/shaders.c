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
