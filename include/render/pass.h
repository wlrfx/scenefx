#ifndef FX_RENDER_PASS_H
#define FX_RENDER_PASS_H

#include <stdbool.h>
#include <wlr/render/pass.h>
#include <wlr/util/box.h>
#include <wlr/render/interface.h>
#include "scenefx/types/fx/shadow_data.h"

struct fx_gles_render_pass {
	struct wlr_render_pass base;
	struct fx_framebuffer *buffer;
	float projection_matrix[9];
	struct fx_render_timer *timer;
};

/**
 * Begin a new render pass with the supplied destination buffer.
 *
 * Callers must call wlr_render_pass_submit() once they are done with the
 * render pass.
 */
struct fx_gles_render_pass *fx_renderer_begin_buffer_pass(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer, struct wlr_output *output,
		const struct wlr_buffer_pass_options *options);

struct fx_render_texture_options {
	struct wlr_render_texture_options base;
	float scale;
	struct wlr_box *clip_box; // Used to clip csd. Ignored if NULL
	int corner_radius;
	bool discard_transparent;
};

struct fx_render_texture_options fx_render_texture_options_default(
		const struct wlr_render_texture_options *base);

struct fx_render_rect_options {
	struct wlr_render_rect_options base;
	float scale;
};

struct fx_render_rect_options fx_render_rect_options_default(
		const struct wlr_render_rect_options *base);

struct fx_render_blur_options {
	struct fx_render_texture_options tex_options;
	struct wlr_scene_buffer *scene_buffer;
	struct wlr_output *output;
	struct wlr_box monitor_box;
	struct fx_framebuffer *current_buffer;
	struct blur_data *blur_data;
	bool ignore_transparent;
};

/**
 * Render a fx texture.
 */
void fx_render_pass_add_texture(struct fx_gles_render_pass *render_pass,
	const struct fx_render_texture_options *options);

/**
 * Render a rectangle.
 */
void fx_render_pass_add_rect(struct fx_gles_render_pass *render_pass,
	const struct fx_render_rect_options *options);

/**
 * Render a stencil mask.
 */
void fx_render_pass_add_stencil_mask(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options, int corner_radius);

/**
 * Render a box shadow.
 */
void fx_render_pass_add_box_shadow(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options,
		int corner_radius, struct shadow_data *shadow_data);

/**
 * Render blur.
 */
void fx_render_pass_add_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_options *fx_options);

/**
 * Render optimized blur.
 */
void fx_render_pass_add_optimized_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_options *fx_options);

/**
 * Render from one buffer to another
 */
void fx_renderer_read_to_buffer(struct fx_gles_render_pass *pass,
		pixman_region32_t *region, struct fx_framebuffer *dst_buffer,
		struct fx_framebuffer *src_buffer);

#endif
