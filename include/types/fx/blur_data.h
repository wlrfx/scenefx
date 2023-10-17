#ifndef TYPES_FX_BLUR_DATA
#define TYPES_FX_BLUR_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>

struct blur_data {
	int num_passes;
	int radius;
};

struct blur_data blur_data_get_default(void);

bool scene_buffer_has_blur(bool backdrop_blur, int radius, int num_passes);

int blur_data_calc_size(int radius, int num_passes);

#endif
