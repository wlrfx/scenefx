#ifndef _FX_EFFECT_FRAMEBUFFERS_H
#define _FX_EFFECT_FRAMEBUFFERS_H

#include <wlr/types/wlr_output.h>
#include <wlr/util/addon.h>

/**
 * Used to add effect framebuffers per output instead of every output sharing
 * them.
 */
struct fx_effect_framebuffers {
	struct wlr_addon addon;

	// Contains the blurred background for tiled windows
	struct fx_framebuffer *optimized_blur_buffer;
	// Contains the original pixels to draw over the areas where artifact are visible
	struct fx_framebuffer *blur_saved_pixels_buffer;
	// Blur swaps between the two effects buffers everytime it scales the image
	// Buffer used for effects
	struct fx_framebuffer *effects_buffer;
	// Swap buffer used for effects
	struct fx_framebuffer *effects_buffer_swapped;

	// The region where there's blur
	pixman_region32_t blur_padding_region;
};

struct fx_effect_framebuffers *fx_effect_framebuffers_try_get(struct wlr_output *output);

#endif
