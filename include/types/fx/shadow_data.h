#ifndef TYPES_FX_SHADOW_DATA
#define TYPES_FX_SHADOW_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>

struct shadow_data {
	bool enabled;
	float *color;
	float blur_sigma;
};

struct shadow_data shadow_data_get_default(void);

bool scene_buffer_has_shadow(struct shadow_data *data);

#endif
