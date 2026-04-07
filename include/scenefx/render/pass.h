#ifndef SCENE_FX_RENDER_PASS_H
#define SCENE_FX_RENDER_PASS_H

#include <wlr/render/pass.h>
#include <wlr/types/wlr_output.h>

#include "types/fx/clipped_region.h"

struct fx_renderer;

struct fx_gradient {
	float degree;
	/* The full area the gradient fit too, for borders use the window size */
	struct wlr_box range;
	/* The center of the gradient, {0.5, 0.5} for normal*/
	float origin[2];
	/* 1 = Linear, 2 = Conic */
	int linear;
	/* Whether or not to blend the colors */
	int blend;
	int count;
	float *colors;
};

struct fx_render_texture_options {
	struct wlr_render_texture_options base;
	const struct wlr_box *clip_box; // Used to clip csd. Ignored if NULL
	struct fx_corner_fradii corners;
	bool discard_transparent;
	struct clipped_fregion clipped_region;
};

struct fx_render_rect_options {
	struct wlr_render_rect_options base;
	struct clipped_fregion clipped_region;
};

struct fx_render_rect_grad_options {
	struct wlr_render_rect_options base;
	struct fx_gradient gradient;
};

struct fx_render_rounded_rect_options {
	struct wlr_render_rect_options base;
	struct fx_corner_fradii corners;
	struct clipped_fregion clipped_region;
};

struct fx_render_rounded_rect_grad_options {
	struct wlr_render_rect_options base;
	struct fx_gradient gradient;
	struct fx_corner_fradii corners;
};

struct fx_render_box_shadow_options {
	struct wlr_box box;
	struct clipped_fregion clipped_region;
	/* Clip region, leave NULL to disable clipping */
	const pixman_region32_t *clip;

	float blur_sigma;
	int corner_radius;
	struct wlr_render_color color;
};

struct fx_render_blur_pass_options {
	struct fx_render_texture_options tex_options;
	struct wlr_buffer *current_buffer;
	struct blur_data *blur_data;
	bool use_optimized_blur;
	bool ignore_transparent;
	float blur_strength;
	struct fx_corner_fradii corners;
	struct clipped_fregion clipped_region;
};

/**
 * Initializes the render pass offscreen buffers required for advanced effects
 * like blur.
 */
struct fx_render_pass *fx_renderer_init_render_pass(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *wlr_render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output);

void fx_render_pass_destroy(struct fx_render_pass *fx_pass);

/**
 * Render a fx texture.
 */
void fx_render_pass_add_texture(struct fx_render_pass *render_pass,
	const struct fx_render_texture_options *fx_options);

/**
 * Render a rectangle.
 */
void fx_render_pass_add_rect(struct fx_render_pass *render_pass,
	const struct fx_render_rect_options *fx_options);

/**
 * Render a rectangle with a gradient.
 */
void fx_render_pass_add_rect_grad(struct fx_render_pass *render_pass,
	const struct fx_render_rect_grad_options *fx_options);

/**
 * Render a rounded rectangle.
 */
void fx_render_pass_add_rounded_rect(struct fx_render_pass *render_pass,
	const struct fx_render_rounded_rect_options *fx_options);

/**
 * Render a rounded rectangle with a gradient.
 */
void fx_render_pass_add_rounded_rect_grad(struct fx_render_pass *render_pass,
	const struct fx_render_rounded_rect_grad_options *fx_options);

/**
 * Render a box shadow.
 */
void fx_render_pass_add_box_shadow(struct fx_render_pass *pass,
		const struct fx_render_box_shadow_options *fx_options);

/**
 * Render blur.
 */
void fx_render_pass_add_blur(struct fx_render_pass *pass,
		const struct fx_render_blur_pass_options *fx_options);

/**
 * Render optimized blur.
 */
bool fx_render_pass_add_optimized_blur(struct fx_render_pass *pass,
		const struct fx_render_blur_pass_options *fx_options);

/**
 * Render from one buffer to another
 */
void fx_render_pass_read_to_buffer(struct fx_render_pass *pass,
		pixman_region32_t *region, struct wlr_buffer *dst_buffer,
		struct wlr_buffer *src_buffer);

void fx_render_pass_save_blur_region(struct fx_render_pass *render_pass,
		pixman_region32_t *region);

void fx_render_pass_apply_saved_blur_region(struct fx_render_pass *render_pass,
		pixman_region32_t *region);

#endif
