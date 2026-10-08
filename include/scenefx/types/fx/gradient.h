#ifndef TYPES_FX_GRADIENT_H
#define TYPES_FX_GRADIENT_H

#include "wlr/util/box.h"

#include <stdint.h>

enum fx_gradient_kind {
	FX_GRADIENT_LINEAR = 0,
	FX_GRADIENT_RADIAL = 1,
	FX_GRADIENT_CONIC = 2,
};

// Defines a color gradient. The gradients follow the CSS conventions.
struct fx_gradient {
	enum fx_gradient_kind kind;
	// The angle of clockwise rotation in degrees. Does not affect
	// `FX_GRADIENT_RADIAL`.
    float angle;
	// The size of the box the gradient is rendered in. For borders use the
	// window size.
    struct wlr_box range; 
	// The center of the gradient in [0.0, 1.0]^2. { 0.5, 0.5 } for centered.
	// Only applies to `FX_GRADIENT_RADIAL` and `FX_GRADIENT_CONIC`.
    float origin[2]; 
	// Whether to smoothly blend colors. If disabled, the gradients will have
	// hard color stops.
    bool blend;
    int32_t colors_size;
	// The components of the colors. Each color must have 4 components.
	// The order of the colors with `angle` being 0deg is as follows:
	// - LINEAR: the first color is at the bottom of the box, the last at the
	//           top of the box.
	// - RADIAL: the first color is the innermost, the last color is the
	//           outermost.
	// - CONIC:  the first color is right of the seam, the last is left of the
	//           seam and colors follow the direction of the clock.
    float *colors;
};

bool fx_gradient_compare_equal(struct fx_gradient const* lhs, struct fx_gradient const* rhs);

#endif // TYPES_FX_GRADIENT_H
