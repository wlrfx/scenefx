#ifndef SCENE_FX_RENDER_PASS_H
#define SCENE_FX_RENDER_PASS_H

#include <stdbool.h>
#include <wlr/render/pass.h>
#include <wlr/render/interface.h>
#include "scenefx/types/fx/shadow_data.h"

struct fx_gles_render_pass {
	struct wlr_render_pass base;
	struct fx_framebuffer *buffer;
	float projection_matrix[9];
	struct fx_render_timer *timer;
};

struct fx_render_texture_options {
	struct wlr_render_texture_options base;
	float scale;
	struct wlr_box *clip_box; // Used to clip csd. Ignored if NULL
	int corner_radius;
	bool discard_transparent;
};

struct fx_render_rect_options {
	struct wlr_render_rect_options base;
	float scale;
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
 * Render a box shadow.
 */
void fx_render_pass_add_box_shadow(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options,
		int corner_radius, struct shadow_data *shadow_data);

#endif
