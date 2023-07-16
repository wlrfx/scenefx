#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/decoration_data.h"
#include "wlr/util/log.h"

struct decoration_data decoration_data_get_undecorated(void) {
	return (struct decoration_data) {
		.alpha = 1.0f,
		.dim = 0.0f,
		.dim_color = default_dim_color,
		.corner_radius = 0,
		.saturation = 1.0f,
		.has_titlebar = false,
		.blur = false,
		.shadow = false,
	};
}
