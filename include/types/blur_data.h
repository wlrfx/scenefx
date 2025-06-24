#ifndef TYPES_FX_BLUR_DATA_H
#define TYPES_FX_BLUR_DATA_H

#include <stdbool.h>
#include <wlr/util/addon.h>

struct blur_data {
	int num_passes;
	int radius;
	float noise;
	float brightness;
	float contrast;
	float saturation;
};

struct blur_data blur_data_get_default(void);

bool is_scene_blur_enabled(struct blur_data *blur_data);

bool blur_data_should_parameters_blur_effects(struct blur_data *blur_data);

int blur_data_calc_size(struct blur_data *blur_data);

#endif
