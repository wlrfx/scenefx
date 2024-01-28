#ifndef TYPES_FX_BLUR_DATA_H
#define TYPES_FX_BLUR_DATA_H

#include <stdbool.h>
#include <wlr/util/addon.h>

struct blur_data {
	int num_passes;
	int radius;
	bool ignore_transparent;
};

struct blur_data blur_data_get_default(void);

bool scene_buffer_should_blur(bool backdrop_blur, struct blur_data *blur_data);

int blur_data_calc_size(struct blur_data *blur_data);

#endif
