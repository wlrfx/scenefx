#ifndef _RENDERER_VULKAN_VULKAN_H
#define _RENDERER_VULKAN_VULKAN_H

#include <vulkan/vulkan_core.h>
#include <wayland-util.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/wlr_renderer.h>

#include "render/fx_renderer.h"
#include "render/vulkan/pipeline.h"
#include "render/vulkan/shaders.h"
// #include "render/tracy.h"

struct vk_renderer;
struct vk_render_setup;

extern const struct fx_renderer_tracy_impl vk_renderer_tracy_impl;

struct vk_buffer {
	struct wlr_buffer *wlr_buffer;

	struct vk_renderer *vk_renderer;
	struct wl_list link; // vk_renderer.buffers

	// TODO: Stenciling

	struct wlr_addon addon;
};

struct vk_buffer *vk_buffer_get_or_create(struct fx_renderer *fx_renderer,
		struct wlr_buffer *wlr_buffer, bool needs_stencil);
void vk_buffer_get_or_allocate(struct fx_renderer *fx_renderer,
		struct wlr_allocator *allocator, int width, int height, bool has_alpha,
		struct vk_buffer **vk_buffer, bool *failed);
/** Note: Doesn't drop the wlr_buffer, so should only be used internally. */
void vk_buffer_destroy(struct vk_buffer *buffer);

struct vk_offscreen_buffers {
	struct fx_offscreen_buffers fx_offscreen_buffers;

	// Contains the blurred background for tiled windows
	struct vk_buffer *optimized_blur_buffer;
	// Contains the non-blurred background for tiled windows. Used for blurring
	// optimized surfaces with an alpha. Just as inefficient as the regular blur.
	struct vk_buffer *optimized_no_blur_buffer;
	// Contains the original pixels to draw over the areas where artifact are visible
	struct vk_buffer *blur_saved_pixels_buffer;
	// Blur swaps between the two effects buffers every time it scales the image
	// Buffer used for effects
	struct vk_buffer *effects_buffer;
	// Swap buffer used for effects
	struct vk_buffer *effects_buffer_swapped;
};

struct vk_offscreen_buffers *vk_get_offscreen_buffers(
		struct fx_offscreen_buffers *offscreen_buffers);

struct vk_render_pass {
	struct fx_render_pass fx_render_pass;
	struct vk_renderer *vk_renderer;

	struct vk_buffer *render_buffer;
	// Upstream uses different VkRenderPasses for each type of rendering pathway
	struct vk_render_setup *render_setup;
	VkCommandBuffer command_buffer;
	VkPipeline bound_pipeline;
	float projection[9];

	// struct wlr_color_transform *color_transform;
	// struct wlr_drm_syncobj_timeline *signal_timeline;
	// uint64_t signal_point;
	// struct wl_array textures; // struct wlr_vk_render_pass_texture

	// Contains output-specific framebuffers.
	// NULL when no advanced effects like blur is being used in the current pass.
	// Call `fx_render_pass_init_offscreen_buffers` to use advanced effects.
	struct vk_offscreen_buffers *vk_offscreen_buffers;
};

struct vk_render_pass *vk_get_render_pass(struct fx_render_pass *fx_render_pass);

struct fx_render_pass *vk_render_pass_init(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output);

// Two targets are enough: the dual-Kawase chain only ever ping-pongs between a
// source and a destination.
#define VK_EFFECT_IMAGE_COUNT 2

/**
 * Offscreen colour target for the blur chain. Full output size: the dual-Kawase
 * down/up scaling is expressed by shrinking the scissor rather than the image,
 * matching the GLES2 implementation (and what blur1.frag's `uv * 2.0` expects).
 */
struct vk_effect_image {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkFramebuffer framebuffer;
	VkDescriptorSet ds;
	uint32_t width, height;
	// Tracked so the barrier before each use knows the source layout. The scene
	// render pass declares finalLayout SHADER_READ_ONLY_OPTIMAL, so after any
	// use the image sits there; before first use it is still UNDEFINED.
	VkImageLayout layout;
};

struct vk_render_setup {
	struct wl_list link; // vk_renderer.render_setups
	VkRenderPass render_pass;

	// Contains all different per-shader pipelines
	struct vk_pipelines vk_pipelines;
	struct wl_list pipelines; // struct vk_pipeline.link
};

struct vk_render_setup *vk_render_setup_find_or_create(struct vk_renderer *renderer,
		struct wlr_render_pass *render_pass);
void vk_render_setup_destroy(struct vk_renderer *renderer, struct vk_render_setup *render_setup);

struct vk_renderer {
	struct fx_renderer fx_renderer;

	VkDevice device;
	VkPhysicalDevice physical_device;
	VkQueue queue;

	struct {
		VkShaderModule vert;

		struct vk_shader_info quad;

		// Sampling path owned by scenefx. The blur passes sample our own
		// offscreen images rather than wlr_textures, so we cannot reuse
		// wlroots' texture descriptor sets: its layout declares an immutable
		// sampler, and Vulkan only treats layouts as compatible when they are
		// identically defined, immutable samplers included.
		// Dual-Kawase also depends on bilinear taps, hence LINEAR/CLAMP_TO_EDGE.
		VkSampler tex_sampler;
		VkDescriptorSetLayout tex_ds_layout;
		VkDescriptorPool tex_ds_pool;

		struct vk_shader_info blur1;
		struct vk_shader_info blur2;
		struct vk_shader_info blur_effects;

		// TODO: more shaders
		// struct quad_grad_shader quad_grad;
		// struct quad_grad_round_shader quad_grad_round;
		//
		// struct tex_shader tex_rgba;
		// struct tex_shader tex_rgbx;
		// struct tex_shader tex_ext;
		//
		// struct box_shadow_shader box_shadow;
	} shader_info;

	struct wl_list render_setups; // struct vk_render_setup.link
	struct wl_list buffers; // vk_buffer.link

	// Ping-pong render targets for the blur chain. Their framebuffers are built
	// against the *scene* render pass rather than a private one: Vulkan only
	// treats render passes as compatible when they are identical apart from a
	// short exemption list that does NOT include subpass dependencies, so a
	// bespoke pass would make the blur pipelines unusable. See effects.c.
	VkRenderPass effect_render_pass;
	struct vk_effect_image effect_images[VK_EFFECT_IMAGE_COUNT];

	// Holds a copy of the frame's pixels in the blur padding ring, taken before
	// the frame renders and pasted back afterwards. Transfer-only: no
	// framebuffer or descriptor set, since nothing ever draws with it.
	struct {
		VkImage image;
		VkDeviceMemory memory;
		uint32_t width, height;
		bool initialised;
	} saved_pixels;

	// Cached sampler descriptor set for the wlroots blend image, rebuilt only
	// when the underlying image view changes.
	VkDescriptorSet blend_ds;
	VkImageView blend_ds_view;
};

bool vk_effect_images_ensure(struct vk_renderer *renderer,
	VkRenderPass render_pass, uint32_t width, uint32_t height);
void vk_effect_images_finish(struct vk_renderer *renderer);
/** Barrier the effect image into COLOR_ATTACHMENT_OPTIMAL ready to be drawn to. */
void vk_effect_image_prepare_target(VkCommandBuffer cb, struct vk_effect_image *img);

/** (Re)create the blur-padding snapshot image at the given size. */
bool vk_saved_pixels_ensure(struct vk_renderer *renderer,
	uint32_t width, uint32_t height);
void vk_saved_pixels_finish(struct vk_renderer *renderer);
VkDescriptorSet vk_get_blend_ds(struct vk_renderer *renderer, VkImageView view);

/** Index of a memory type satisfying props, or -1. */
int vk_find_mem_type(struct vk_renderer *renderer,
	VkMemoryPropertyFlags props, uint32_t type_bits);

struct fx_renderer *vk_renderer_create(struct wlr_renderer *wlr_renderer);

struct vk_renderer *vk_get_renderer(struct fx_renderer *fx_renderer);

const char *vulkan_strerror(VkResult err);

#if __STDC_VERSION__ >= 202311L

#define wlr_vk_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res __VA_OPT__(,) __VA_ARGS__)

#else

#define wlr_vk_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

#endif

#endif
