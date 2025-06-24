#include "scenefx/types/fx/blur_data.h"

struct blur_data blur_data_get_default(void) {
	return (struct blur_data) {
		.radius = 5,
		.num_passes = 3,
		.noise = 0.02,
		.brightness = 0.9,
		.contrast = 0.9,
		.saturation = 1.1,
	};
}

bool is_scene_blur_enabled(struct blur_data *blur_data) {
	return blur_data->radius > 0 && blur_data->num_passes > 0;
}

bool blur_data_should_parameters_blur_effects(struct blur_data *blur_data) {
	return blur_data->brightness != 1.0f
		|| blur_data->saturation != 1.0f
		|| blur_data->contrast != 1.0f
		|| blur_data->noise > 0.0f;
}

int blur_data_calc_size(struct blur_data *blur_data) {
	return pow(2, blur_data->num_passes + 1) * blur_data->radius;
}
