#include "scenefx/types/fx/shadow_data.h"

struct shadow_data shadow_data_get_default(void) {
	return (struct shadow_data) {
		.blur_sigma = 20,
		.color = {0.0f, 0.0f, 0.0f, 0.5f},
		.enabled = false,
		.offset_x = 0.0f,
		.offset_y = 0.0f,
	};
}

bool scene_buffer_has_shadow(struct shadow_data *data) {
	return data->enabled && data->blur_sigma > 0 && data->color.a > 0.0;
}
