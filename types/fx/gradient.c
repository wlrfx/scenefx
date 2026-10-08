#include "scenefx/types/fx/gradient.h"

#include <string.h>

bool fx_gradient_compare_equal(struct fx_gradient const *lhs,
		struct fx_gradient const *rhs) {
	if (lhs->kind != rhs->kind) {
		return false;
	}

	if (memcmp(&lhs->range, &rhs->range, sizeof(struct wlr_box)) != 0) {
		return false;
	}

	if (lhs->blend != rhs->blend) {
		return false;
	}

	if (lhs->colors_size != rhs->colors_size) {
		return false;
	}

	int32_t const color_components = 4 * sizeof(float) * lhs->colors_size;
	if (memcmp(lhs->colors, rhs->colors, color_components) != 0) {
		return false;
	}

	switch (lhs->kind) {
		case FX_GRADIENT_LINEAR: {
			if (lhs->angle != rhs->angle) {
				return false;
			}
		} break;
		case FX_GRADIENT_RADIAL: {
			if (lhs->origin[0] != rhs->origin[0] || lhs->origin[1] != rhs->origin[1]) {
				return false;
			}
		} break;
		case FX_GRADIENT_CONIC: {
			if (lhs->angle != rhs->angle || lhs->origin[0] != rhs->origin[0] ||
					lhs->origin[1] != rhs->origin[1]) {
				return false;
			}
		} break;
	}

	return true;
}
