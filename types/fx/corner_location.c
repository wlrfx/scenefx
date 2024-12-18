#include "scenefx/types/fx/corner_location.h"

// Maps the corners to its correct transformed corner
static const enum corner_location corner_transform[8][CORNER_LOCATION_LAST] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_TOP_LEFT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_TOP_RIGHT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_BOTTOM_RIGHT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_BOTTOM_LEFT,
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_BOTTOM_LEFT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_TOP_LEFT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_TOP_RIGHT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_BOTTOM_RIGHT,
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_BOTTOM_RIGHT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_BOTTOM_LEFT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_TOP_LEFT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_TOP_RIGHT,
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_TOP_RIGHT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_BOTTOM_RIGHT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_BOTTOM_LEFT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_TOP_LEFT,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_TOP_RIGHT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_TOP_LEFT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_BOTTOM_LEFT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_BOTTOM_RIGHT,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_TOP_LEFT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_BOTTOM_LEFT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_BOTTOM_RIGHT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_TOP_RIGHT,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_BOTTOM_LEFT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_BOTTOM_RIGHT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_TOP_RIGHT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_TOP_LEFT,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		[CORNER_LOCATION_TOP_LEFT] = CORNER_LOCATION_BOTTOM_RIGHT,
		[CORNER_LOCATION_TOP_RIGHT] = CORNER_LOCATION_TOP_RIGHT,
		[CORNER_LOCATION_BOTTOM_RIGHT] = CORNER_LOCATION_TOP_LEFT,
		[CORNER_LOCATION_BOTTOM_LEFT] = CORNER_LOCATION_BOTTOM_LEFT,
	},
};

void corner_location_transform(enum wl_output_transform transform,
		enum corner_location *corners) {
	enum corner_location result = CORNER_LOCATION_NONE;

	int bitmask = 1 << (CORNER_LOCATION_COUNT - 1);
	int mask = 1;
	while (bitmask) {
		switch (*corners & mask) {
		case CORNER_LOCATION_NONE:
		case CORNER_LOCATION_ALL:
			break;
		default:
			result |= corner_transform[transform][*corners & mask];
			break;
		}

		bitmask &= ~mask;
		mask <<= 1;
	}

	*corners = result;
}
