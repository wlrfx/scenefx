#include "scenefx/types/fx/clipped_region.h"
#include "types/fx/clipped_region.h"

struct fx_corner_radii fx_corner_radii_extend(struct fx_corner_radii corners, int extend) {
	return (struct fx_corner_radii) {
		.top_left =  corners.top_left == 0 ? 0 : (corners.top_left + extend),
		.top_right = corners.top_right == 0 ? 0 : (corners.top_right + extend),
		.bottom_left = corners.bottom_left == 0 ? 0 : (corners.bottom_left + extend),
		.bottom_right = corners.bottom_right == 0 ? 0 : (corners.bottom_right + extend),
	};
}

void fx_corner_radii_transform(enum wl_output_transform transform,
		struct fx_corner_radii *corners) {
	if (transform & WL_OUTPUT_TRANSFORM_FLIPPED) {
		*corners = (struct fx_corner_radii){
			corners->top_right,
			corners->top_left,
			corners->bottom_left,
			corners->bottom_right
		};
	}

	unsigned int turns = transform & (WL_OUTPUT_TRANSFORM_90 | WL_OUTPUT_TRANSFORM_180 | WL_OUTPUT_TRANSFORM_270);
	if (turns > 0) {
		uint16_t points[4] = {corners->top_left, corners->top_right, corners->bottom_right, corners->bottom_left};
		*corners = (struct fx_corner_radii){
			points[turns % 4],
			points[(turns + 1) % 4],
			points[(turns + 2) % 4],
			points[(turns + 3) % 4],
		};
	}
}

struct fx_corner_fradii fx_corner_radii_scale(struct fx_corner_radii corners, float scale) {
	return (struct fx_corner_fradii) {
	(float)corners.top_left * scale,
	(float)corners.top_right * scale,
	(float)corners.bottom_right * scale,
	(float)corners.bottom_left * scale,
	};
}

bool fx_corner_radii_eq(const struct fx_corner_radii lhs, const struct fx_corner_radii rhs) {
	return lhs.top_left == rhs.top_left
		&& lhs.top_right == rhs.top_right
		&& lhs.bottom_right == rhs.bottom_right
		&& lhs.bottom_left == rhs.bottom_left;
}

bool fx_corner_radii_is_empty(const struct fx_corner_radii* corners) {
	return corners->top_left == 0
		&& corners->top_right == 0
		&& corners->bottom_right == 0
		&& corners->bottom_left == 0;
}

bool fx_corner_fradii_is_empty(const struct fx_corner_fradii* corners) {
	return corners->top_left == 0.0
		&& corners->top_right == 0.0
		&& corners->bottom_right == 0.0
		&& corners->bottom_left == 0.0;
}

struct clipped_region clipped_region_get_default(void) {
	return (struct clipped_region) {
		.corners = {0},
		.area = (struct wlr_box) {0},
	};
}
