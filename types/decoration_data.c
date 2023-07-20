#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/decoration_data.h"
#include "wlr/util/log.h"

struct shadow_data shadow_data_get_default(void) {
	return (struct shadow_data) {
		.blur_sigma = 20,
		.color = default_shadow_color,
		.enabled = false,
	};
}

bool shadow_data_is_enabled(struct shadow_data *data) {
	return data->enabled && data->blur_sigma > 0 && data->color[3] > 0.0;
}

struct decoration_data decoration_data_get_undecorated(void) {
	return (struct decoration_data) {
		.alpha = 1.0f,
		.dim = 0.0f,
		.dim_color = default_dim_color,
		.corner_radius = 0,
		.saturation = 1.0f,
		.has_titlebar = false,
		.blur = false,
		.shadow_data = shadow_data_get_default(),
	};
}
