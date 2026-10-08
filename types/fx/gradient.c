#include "scenefx/types/fx/gradient.h"

#include <string.h>

bool gradient_compare_equal(struct gradient const *lhs,
		struct gradient const *rhs) {
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

	if (memcmp(lhs->colors, rhs->colors, sizeof(float) * lhs->colors_size) != 0) {
		return false;
	}

	switch (lhs->kind) {
		case GRADIENT_LINEAR: {
			if (lhs->angle != rhs->angle) {
				return false;
			}
		} break;
		case GRADIENT_RADIAL: {
			if (lhs->origin[0] != rhs->origin[0] || lhs->origin[1] != rhs->origin[1]) {
				return false;
			}
		} break;
		case GRADIENT_CONIC: {
			if (lhs->angle != rhs->angle || lhs->origin[0] != rhs->origin[0] ||
					lhs->origin[1] != rhs->origin[1]) {
				return false;
			}
		} break;
	}

	return true;
}
