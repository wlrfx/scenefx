#include "scenefx/types/fx/corner_location.h"
#include "scenefx/types/fx/hole_data.h"

struct hole_data hole_data_get_default(void) {
	return (struct hole_data) {
		.corner_radius = 0,
		.size = (struct wlr_box) {0},
	};
}
