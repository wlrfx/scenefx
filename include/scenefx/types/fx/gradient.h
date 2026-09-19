#ifndef TYPES_FX_GRADIENT_H
#define TYPES_FX_GRADIENT_H

#include "wlr/util/box.h"

#include <stdint.h>

enum gradient_kind {
	GRADIENT_LINEAR = 0,
	GRADIENT_RADIAL = 1,
	GRADIENT_CONIC = 2,
};

struct gradient {
	enum gradient_kind kind;
    float angle;
	// The full area the gradient fit to. For borders use the window size.
    struct wlr_box range; 
	// The center of the gradient in [0.0, 1.0]^2. { 0.5, 0.5 } for centered.
    float origin[2]; 
    bool blend;
    int32_t colors_size;
    float *colors;
};

bool gradient_compare_equal(struct gradient const* lhs, struct gradient const* rhs);

#endif // TYPES_FX_GRADIENT_H
