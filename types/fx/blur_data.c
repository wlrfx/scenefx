#include "scenefx/types/fx/blur_data.h"

struct blur_data blur_data_get_default(void) {
	return (struct blur_data) {
		.radius = 5.0f,
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

void blur_data_apply_alpha(struct blur_data *blur_data, const float *a) {
	if (!a) {
		return;
	}
	float alpha = *a;

	if (alpha >= 1.0f) {
		return;
	} else if (alpha <= 0.0f) {
		blur_data->num_passes = 0;
		blur_data->radius = 0.0f;
		return;
	}

	// Calculate the new blur strength from the new alpha multiplied blur size
	// Also make sure that the values are never larger than the initial values
	struct blur_data ref_blur_data = *blur_data;
	int new_size = blur_data_calc_size(&ref_blur_data) * alpha;
	blur_data->num_passes = fmax(0, fmin(ref_blur_data.num_passes, ceilf(log2(new_size / ref_blur_data.radius))));
	blur_data->radius = fmax(0, fmin(ref_blur_data.radius, new_size / pow(2, blur_data->num_passes + 1)));
}
