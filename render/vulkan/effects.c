// Offscreen render targets for the blur ping-pong chain.
//
// These are plain VkImages owned by the renderer rather than wlr_buffers: blur
// is recorded into the *same* command buffer as the scene (between
// wlr_vk_render_pass_suspend/resume), so keeping the targets internal avoids
// DMA-BUF import and any cross-command-buffer synchronisation.
//
// Format matches the scene render pass' colour attachment (the 16F blend
// image). That is deliberate: a Vulkan pipeline may be used with any
// "compatible" render pass, and compatibility only requires matching attachment
// formats and subpass structure, so the blur pipelines built against the scene
// pass can be reused here unchanged.

#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include <wlr/render/vulkan.h>
#include <wlr/util/log.h>

#include "render/vulkan/shaders.h"
#include "render/vulkan/vulkan.h"

#define EFFECT_IMAGE_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

// wlroots keeps its equivalent private, so scenefx needs its own.
int vk_find_mem_type(struct vk_renderer *renderer,
		VkMemoryPropertyFlags props, uint32_t type_bits) {
	VkPhysicalDeviceMemoryProperties mem_props;
	vkGetPhysicalDeviceMemoryProperties(renderer->physical_device, &mem_props);

	for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
		if (!(type_bits & (1u << i))) {
			continue;
		}
		if ((mem_props.memoryTypes[i].propertyFlags & props) == props) {
			return (int)i;
		}
	}
	return -1;
}

static void effect_image_finish(struct vk_renderer *renderer,
		struct vk_effect_image *img) {
	VkDevice dev = renderer->device;
	if (img->image == VK_NULL_HANDLE && img->ds == VK_NULL_HANDLE) {
		return;
	}
	// These are only torn down on output reconfigure or shutdown, but the
	// previous frame's command buffer may still reference the descriptor set,
	// and freeing one that is in use is invalid. Rare enough that a full idle
	// wait is cheaper than tracking per-frame lifetimes.
	vkDeviceWaitIdle(dev);
	if (img->ds != VK_NULL_HANDLE) {
		vk_tex_ds_destroy(renderer, img->ds);
	}
	vkDestroyFramebuffer(dev, img->framebuffer, NULL);
	vkDestroyImageView(dev, img->view, NULL);
	vkDestroyImage(dev, img->image, NULL);
	vkFreeMemory(dev, img->memory, NULL);
	*img = (struct vk_effect_image){0};
}

static bool effect_image_init(struct vk_renderer *renderer,
		struct vk_effect_image *img, uint32_t width, uint32_t height) {
	VkDevice dev = renderer->device;
	VkResult res;

	VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = EFFECT_IMAGE_FORMAT,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.extent = (VkExtent3D){ width, height, 1 },
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	};
	res = vkCreateImage(dev, &img_info, NULL, &img->image);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkCreateImage failed for effect image");
		goto error;
	}

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(dev, img->image, &mem_reqs);

	int mem_type = vk_find_mem_type(renderer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		mem_reqs.memoryTypeBits);
	if (mem_type < 0) {
		wlr_log(WLR_ERROR, "no suitable memory type for effect image");
		goto error;
	}

	VkMemoryAllocateInfo mem_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = mem_type,
	};
	res = vkAllocateMemory(dev, &mem_info, NULL, &img->memory);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkAllocateMemory failed for effect image");
		goto error;
	}

	res = vkBindImageMemory(dev, img->image, img->memory, 0);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkBindImageMemory failed for effect image");
		goto error;
	}

	VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = img->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = EFFECT_IMAGE_FORMAT,
		.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1,
		},
	};
	res = vkCreateImageView(dev, &view_info, NULL, &img->view);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkCreateImageView failed for effect image");
		goto error;
	}

	VkFramebufferCreateInfo fb_info = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = renderer->effect_render_pass,
		.attachmentCount = 1,
		.pAttachments = &img->view,
		.width = width,
		.height = height,
		.layers = 1,
	};
	res = vkCreateFramebuffer(dev, &fb_info, NULL, &img->framebuffer);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkCreateFramebuffer failed for effect image");
		goto error;
	}

	img->ds = vk_tex_ds_create(renderer, img->view);
	if (img->ds == VK_NULL_HANDLE) {
		wlr_log(WLR_ERROR, "failed to create descriptor set for effect image");
		goto error;
	}

	img->width = width;
	img->height = height;
	return true;

error:
	effect_image_finish(renderer, img);
	return false;
}

bool vk_effect_images_ensure(struct vk_renderer *renderer,
		VkRenderPass render_pass, uint32_t width, uint32_t height) {
	if (render_pass == VK_NULL_HANDLE) {
		return false;
	}
	// Framebuffers are tied to the render pass they were created with, so a
	// different scene pass (e.g. after an output format change) invalidates them.
	if (renderer->effect_render_pass != render_pass) {
		vk_effect_images_finish(renderer);
		renderer->effect_render_pass = render_pass;
	}

	for (size_t i = 0; i < VK_EFFECT_IMAGE_COUNT; i++) {
		struct vk_effect_image *img = &renderer->effect_images[i];
		if (img->image != VK_NULL_HANDLE &&
				img->width == width && img->height == height) {
			continue;
		}
		effect_image_finish(renderer, img);
		if (!effect_image_init(renderer, img, width, height)) {
			return false;
		}
	}
	return true;
}

void vk_effect_images_finish(struct vk_renderer *renderer) {
	for (size_t i = 0; i < VK_EFFECT_IMAGE_COUNT; i++) {
		effect_image_finish(renderer, &renderer->effect_images[i]);
	}
	// effect_render_pass is owned by wlroots (it is the scene pass); just forget it.
	renderer->effect_render_pass = VK_NULL_HANDLE;
	if (renderer->blend_ds != VK_NULL_HANDLE) {
		vk_tex_ds_destroy(renderer, renderer->blend_ds);
		renderer->blend_ds = VK_NULL_HANDLE;
		renderer->blend_ds_view = VK_NULL_HANDLE;
	}
}

VkDescriptorSet vk_get_blend_ds(struct vk_renderer *renderer, VkImageView view) {
	if (view == VK_NULL_HANDLE) {
		return VK_NULL_HANDLE;
	}
	// The blend image is recreated when the output is reconfigured, so cache
	// the descriptor set and rebuild it only when the view actually changes.
	if (renderer->blend_ds != VK_NULL_HANDLE && renderer->blend_ds_view == view) {
		return renderer->blend_ds;
	}
	if (renderer->blend_ds != VK_NULL_HANDLE) {
		// Same in-use hazard as effect_image_finish().
		vkDeviceWaitIdle(renderer->device);
		vk_tex_ds_destroy(renderer, renderer->blend_ds);
	}
	renderer->blend_ds = vk_tex_ds_create(renderer, view);
	renderer->blend_ds_view = renderer->blend_ds != VK_NULL_HANDLE ? view : VK_NULL_HANDLE;
	return renderer->blend_ds;
}

void vk_effect_image_prepare_target(VkCommandBuffer cb, struct vk_effect_image *img) {
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = img->layout,
		.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img->image,
		.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1,
		},
		.srcAccessMask = img->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ?
			VK_ACCESS_SHADER_READ_BIT : 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
	};

	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);

	// The scene render pass declares finalLayout SHADER_READ_ONLY_OPTIMAL, so
	// once the upcoming pass ends the image will be samplable again.
	img->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
