#ifndef TYPES_FX_BLUR_DATA_H
#define TYPES_FX_BLUR_DATA_H

#include <stdbool.h>
#include <wlr/util/addon.h>

struct blur_data {
	int num_passes;
	float radius;
	float noise;
	float brightness;
	float contrast;
	float saturation;
};

// num_passes needs to stay low enough that blur_data_calc_size() can't leave int range
#define BLUR_DATA_MAX_NUM_PASSES 10
#define BLUR_DATA_MAX_RADIUS 100

static __always_inline int blur_data_clamp_num_passes(int num_passes) {
	if (num_passes < 0) {
		return 0;
	} else if (num_passes > BLUR_DATA_MAX_NUM_PASSES) {
		return BLUR_DATA_MAX_NUM_PASSES;
	}
	return num_passes;
}

static __always_inline float blur_data_clamp_radius(float radius) {
	if (radius < 0) {
		return 0;
	} else if (radius > BLUR_DATA_MAX_RADIUS) {
		return BLUR_DATA_MAX_RADIUS;
	}
	return radius;
}

struct blur_data blur_data_get_default(void);

bool is_scene_blur_enabled(struct blur_data *blur_data);

bool blur_data_should_parameters_blur_effects(struct blur_data *blur_data);

int blur_data_calc_size(struct blur_data *blur_data);

struct blur_data blur_data_apply_strength(struct blur_data *blur_data, float strength);

#endif
