#ifndef TYPES_FX_BLUR_DATA_H
#define TYPES_FX_BLUR_DATA_H

#include <stdbool.h>
#include <wlr/util/addon.h>

// Calculate the drop-shadows blur sample size
#define drop_shadow_calc_size(blur_sigma) (ceil(1.5f * blur_sigma) * 2)
#define drop_shadow_downscale 0.5

struct blur_data {
	int num_passes;
	float radius;
	float noise;
	float brightness;
	float contrast;
	float saturation;
};

struct blur_data blur_data_get_default(void);

bool is_scene_blur_enabled(struct blur_data *blur_data);

bool blur_data_should_parameters_blur_effects(struct blur_data *blur_data);

int blur_data_calc_size(struct blur_data *blur_data);

struct blur_data blur_data_apply_strength(struct blur_data *blur_data, float strength);

#endif
