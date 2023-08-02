#ifndef TYPES_DECORATION_DATA
#define TYPES_DECORATION_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>

static float default_shadow_color[] = {0.0f, 0.0f, 0.0f, 0.5f};

struct shadow_data {
	bool enabled;
	float *color;
	float blur_sigma;
};

struct shadow_data shadow_data_get_default(void);

bool scene_buffer_has_shadow(struct shadow_data *data);

#endif
