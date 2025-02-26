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

bool blur_data_cmp(struct blur_data *a, struct blur_data *b) {
	return a->radius == b->radius &&
		a->num_passes == b->num_passes &&
		a->noise == b->noise &&
		a->brightness == b->brightness &&
		a->contrast == b->contrast &&
		a->saturation == b->saturation;
}

int blur_data_calc_size(const struct blur_data *blur_data) {
	return pow(2, blur_data->num_passes + 1) * blur_data->radius;
}

struct blur_data blur_data_apply_strength(const struct blur_data *ref_blur_data, float strength) {
	struct blur_data blur_data = *ref_blur_data;

	if (strength >= 1.0f) {
		return blur_data;
	} else if (strength <= 0.0f) {
		blur_data.num_passes = 0;
		blur_data.radius = 0.0f;
		return blur_data;
	}

	// Calculate the new blur strength from the new alpha multiplied blur size
	// Also make sure that the values are never larger than the initial values
	int new_size = blur_data_calc_size(ref_blur_data) * strength;
	blur_data.num_passes = fmax(0, fmin(ref_blur_data->num_passes, ceilf(log2(new_size / ref_blur_data->radius))));
	blur_data.radius = fmax(0, fmin(ref_blur_data->radius, new_size / pow(2, blur_data.num_passes + 1)));

	return blur_data;
}
