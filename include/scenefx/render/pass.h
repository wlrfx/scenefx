#ifndef SCENE_FX_RENDER_PASS_H
#define SCENE_FX_RENDER_PASS_H

#include <stdbool.h>
#include <wlr/render/pass.h>
#include <wlr/render/interface.h>

struct fx_gles_render_pass {
	struct wlr_render_pass base;
	struct fx_framebuffer *buffer;
	struct fx_effect_framebuffers *fx_effect_framebuffers;
	struct wlr_output *output;
	float projection_matrix[9];
	struct fx_render_timer *timer;
};

enum corner_location { TOP_LEFT, TOP_RIGHT, BOTTOM_RIGHT, BOTTOM_LEFT, ALL };

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
	const struct wlr_box *clip_box; // Used to clip csd. Ignored if NULL
	int corner_radius;
	bool has_titlebar;
	bool discard_transparent;
	float dim;
	struct wlr_render_color dim_color;
};

struct fx_render_rect_options {
	struct wlr_render_rect_options base;
	// TODO: Add effects here in the future
};

struct fx_render_box_shadow_options {
	struct wlr_box box;
	struct wlr_box window_box;
	int window_corner_radius;
	/* Clip region, leave NULL to disable clipping */
	const pixman_region32_t *clip;

	float blur_sigma;
	int corner_radius;
	struct wlr_render_color color;
};

struct fx_render_rounded_rect_options {
	struct wlr_render_rect_options base;
	int corner_radius;
	enum corner_location corner_location;
};

struct fx_render_rounded_border_corner_options {
	struct wlr_render_rect_options base;
	int corner_radius;
	int border_thickness;
	enum corner_location corner_location;
};

struct fx_render_blur_pass_options {
	struct fx_render_texture_options tex_options;
	pixman_region32_t *opaque_region;
	struct fx_framebuffer *current_buffer;
	struct blur_data *blur_data;
	bool use_optimized_blur;
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
 * Render a rounded rectangle.
 */
void fx_render_pass_add_rounded_rect(struct fx_gles_render_pass *render_pass,
	const struct fx_render_rounded_rect_options *options);

/**
 * Render a border corner.
 */
void fx_render_pass_add_rounded_border_corner(struct fx_gles_render_pass *render_pass,
	const struct fx_render_rounded_border_corner_options *options);

/**
 * Render a box shadow.
 */
void fx_render_pass_add_box_shadow(struct fx_gles_render_pass *pass,
		const struct fx_render_box_shadow_options *options);

/**
 * Render blur.
 */
void fx_render_pass_add_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options);

/**
 * Render optimized blur.
 */
void fx_render_pass_add_optimized_blur(struct fx_gles_render_pass *pass,
		struct fx_render_blur_pass_options *fx_options);

/**
 * Render from one buffer to another
 */
void fx_renderer_read_to_buffer(struct fx_gles_render_pass *pass,
		pixman_region32_t *region, struct fx_framebuffer *dst_buffer,
		struct fx_framebuffer *src_buffer, bool transformed_region);

#endif
