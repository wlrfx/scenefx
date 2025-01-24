#include "scenefx/types/fx/clipped_region.h"
#include "scenefx/types/fx/corner_location.h"

struct clipped_region clipped_region_get_default(void) {
	return (struct clipped_region) {
		.corner_radius = 0,
		.corners = CORNER_LOCATION_NONE,
		.area = (struct wlr_box) {0},
	};
}
