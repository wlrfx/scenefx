#include "scenefx/types/fx/blur_data.h"

struct blur_data blur_data_get_default(void) {
	return (struct blur_data) {
		.radius = 0,
		.num_passes = 0,
		.ignore_transparent = true,
	};
}

bool scene_buffer_should_blur(bool backdrop_blur, struct blur_data *blur_data) {
	return backdrop_blur && blur_data->radius > 0 && blur_data->num_passes > 0;
}

int blur_data_calc_size(struct blur_data *blur_data) {
	return pow(2, blur_data->num_passes + 1) * blur_data->radius;
}
