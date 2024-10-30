#ifndef TYPES_FX_CORNER_LOCATION_H
#define TYPES_FX_CORNER_LOCATION_H

#include <wayland-server-protocol.h>

#define CORNER_LOCATION_LAST (1 << 3) + 1
#define CORNER_LOCATION_COUNT 5

enum corner_location {
	CORNER_LOCATION_NONE = 0,
	CORNER_LOCATION_TOP_LEFT = 1 << 0,
	CORNER_LOCATION_TOP_RIGHT = 1 << 1,
	CORNER_LOCATION_BOTTOM_RIGHT = 1 << 2,
	CORNER_LOCATION_BOTTOM_LEFT = 1 << 3,
	CORNER_LOCATION_ALL = CORNER_LOCATION_TOP_LEFT
		| CORNER_LOCATION_TOP_RIGHT
		| CORNER_LOCATION_BOTTOM_LEFT
		| CORNER_LOCATION_BOTTOM_RIGHT,
};

void corner_location_transform(enum wl_output_transform transform,
		enum corner_location *corners);

#endif // !TYPES_FX_CORNER_LOCATION_H
