#ifndef TYPES_FX_CLIPPED_REGION_H
#define TYPES_FX_CLIPPED_REGION_H

#include <wlr/util/box.h>

#include "scenefx/types/fx/corner_location.h"

struct clipped_region {
	struct wlr_box area;
	int corner_radius;
	enum corner_location corners;
};

struct clipped_region clipped_region_get_default(void);

#endif // !TYPES_FX_CLIPPED_REGION_H
