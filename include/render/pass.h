#ifndef FX_RENDER_PASS_H
#define FX_RENDER_PASS_H

#include <scenefx/render/pass.h>
#include <stdbool.h>
#include <wlr/render/pass.h>
#include <wlr/util/box.h>
#include <wlr/render/interface.h>

struct fx_render_texture_options fx_render_texture_options_default(
		const struct wlr_render_texture_options *base);

struct fx_render_rect_options fx_render_rect_options_default(
		const struct wlr_render_rect_options *base);

/**
 * Render a stencil mask.
 */
void fx_render_pass_add_stencil_mask(struct fx_gles_render_pass *pass,
		const struct fx_render_rect_options *fx_options, int corner_radius);

#endif
