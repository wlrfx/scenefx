#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/fx/blur_data.h"
#include "wlr/util/log.h"

struct blur_data blur_data_get_default(void) {
	return (struct blur_data) {
		.radius = 0,
		.num_passes = 0,
	};
}

bool scene_buffer_has_blur(bool backdrop_blur, int radius, int num_passes) {
	return backdrop_blur && radius > 0 && num_passes > 0;
}

int blur_data_calc_size(int radius, int num_passes) {
	return pow(2, num_passes) * radius;
}
