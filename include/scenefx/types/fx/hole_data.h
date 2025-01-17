#ifndef TYPES_FX_HOLE_DATA_H
#define TYPES_FX_HOLE_DATA_H

#include <wlr/util/box.h>

#include "scenefx/types/fx/corner_location.h"

// TODO: "hole" might not be the best name
struct hole_data {
	struct wlr_box size;
	int corner_radius;
	enum corner_location corners;
};

struct hole_data hole_data_get_default(void);

#endif // !TYPES_FX_HOLE_DATA_H
