#ifndef _FX_RENDERER_H
#define _FX_RENDERER_H

#include <wlr/render/allocator.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/addon.h>

#include "render/tracy.h"
#include "scenefx/render/pass.h"
#include "scenefx/types/wlr_scene.h"

struct fx_offscreen_buffers;
struct fx_renderer;

/** Find the best supported DRM format */
const struct wlr_drm_format *find_wlr_drm_format(struct wlr_renderer *wlr_renderer, bool alpha);

///
/// fx_offscreen_buffers
///

struct fx_offscreen_buffers_impl {
	void (*destroy)(struct fx_offscreen_buffers *offscreen_buffers);
};

/** Used to add effect buffers per output instead of every output sharing them */
struct fx_offscreen_buffers {
	struct fx_renderer *fx_renderer;
	struct wlr_output *wlr_output;

	struct wl_list link; // fx_renderer.offscreen_buffers
	struct wlr_addon addon;

	const struct fx_offscreen_buffers_impl *impl;
};

void fx_offscreen_buffers_init(struct fx_offscreen_buffers *offscreen_buffers,
		const struct fx_offscreen_buffers_impl *impl,
		struct fx_renderer *fx_renderer, struct wlr_output *output);
void fx_offscreen_buffers_destroy(struct fx_offscreen_buffers *offscreen_buffers);
struct fx_offscreen_buffers *fx_offscreen_buffers_try_get(struct fx_renderer *fx_renderer,
		struct wlr_output *output);

///
/// fx_render_pass
///

struct fx_render_pass_impl {
	void (*destroy)(struct fx_render_pass *fx_pass);
	void (*add_texture)(struct fx_render_pass *render_pass,
			const struct fx_render_texture_options *options);
	void (*add_rect)(struct fx_render_pass *render_pass,
			const struct fx_render_rect_options *options);
	void (*add_rect_grad)(struct fx_render_pass *render_pass,
			const struct fx_render_rect_grad_options *options);
	void (*add_rounded_rect)(struct fx_render_pass *render_pass,
			const struct fx_render_rounded_rect_options *options);
	void (*add_rounded_rect_grad)(struct fx_render_pass *render_pass,
			const struct fx_render_rounded_rect_grad_options *options);
	void (*add_box_shadow)(struct fx_render_pass *pass,
			const struct fx_render_box_shadow_options *options);
	void (*add_blur)(struct fx_render_pass *pass,
			struct fx_render_blur_pass_options *fx_options);
	bool (*add_optimized_blur)(struct fx_render_pass *pass,
			struct fx_render_blur_pass_options *fx_options);
	void (*read_to_buffer)(struct fx_render_pass *pass,
			pixman_region32_t *region, struct wlr_buffer *dst_buffer,
			struct wlr_buffer *src_buffer);
	void (*save_blur_region)(struct fx_render_pass *pass,
			pixman_region32_t *region);
	void (*apply_saved_blur_region)(struct fx_render_pass *pass,
			pixman_region32_t *region);
};

struct fx_render_pass {
	struct fx_renderer *fx_renderer;
	struct wlr_render_pass *render_pass;

	// The region where there's blur
	pixman_region32_t blur_padding_region;
	bool has_blur;

	const struct fx_render_pass_impl *impl;
};

void fx_render_pass_init(struct fx_render_pass *render_pass,
		const struct fx_render_pass_impl *impl, struct fx_renderer *fx_renderer,
		struct wlr_render_pass *wlr_render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output);
void fx_render_pass_destroy(struct fx_render_pass *render_pass);

///
/// fx_renderer
///

struct fx_renderer_impl {
	struct fx_offscreen_buffers *(*offscreen_buffers_allocate)(
			struct fx_renderer *fx_renderer, struct wlr_output *output);
	struct fx_render_pass *(*render_pass_allocate)(struct fx_renderer *fx_renderer,
			struct wlr_render_pass *render_pass, struct wlr_buffer *wlr_buffer,
			struct wlr_output *output);
	void (*renderer_destroy)(struct fx_renderer *fx_renderer);
#ifdef TRACY_ENABLE
	void (*tracy_gpu_zone_begin)(struct fx_renderer *fx_renderer, struct tracy_data *tracy_data,
			struct tracy_gpu_zone_context *out_ctx, const int line,
			const char *source, const char *func, const char *name);
	void (*tracy_gpu_zone_end)(struct fx_renderer *fx_renderer, struct tracy_gpu_zone_context *ctx);
	void (*tracy_gpu_context_collect)(struct fx_renderer *fx_renderer, struct tracy_data *tracy_data);
	void (*tracy_gpu_context_destroy)(struct fx_renderer *fx_renderer, struct tracy_data *tracy_data);
	struct tracy_data *(*tracy_gpu_context_new)(struct fx_renderer *fx_renderer);
#endif
};

/**
 * wlroots OpenGL ES 2 renderer wrapper.
 */

struct fx_renderer {
	struct wlr_renderer *wlr_renderer;

	const struct fx_renderer_impl *impl;

#ifdef TRACY_ENABLE
	struct tracy_data *tracy_data;
	bool tracy_capable;
#endif

	struct wl_list offscreen_buffers; // fx_offscreen_buffers.link

	struct wl_listener renderer_destroy;
};

void fx_renderer_init(struct fx_renderer *fx_renderer,
		const struct fx_renderer_impl *impl, struct wlr_renderer *wlr_renderer);

#endif
