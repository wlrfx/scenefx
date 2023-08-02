#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/fx/shadow_data.h"
#include "wlr/util/log.h"

struct shadow_data shadow_data_get_default(void) {
	return (struct shadow_data) {
		.blur_sigma = 20,
		.color = default_shadow_color,
		.enabled = false,
	};
}

bool scene_buffer_has_shadow(struct shadow_data *data) {
	return data->enabled && data->blur_sigma > 0 && data->color[3] > 0.0;
}
