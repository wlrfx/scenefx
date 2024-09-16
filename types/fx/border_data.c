#include "scenefx/types/fx/border_data.h"

struct border_data border_data_get_default(void) {
	return (struct border_data) {
		.color = {0.0f, 0.0f, 0.0f, 0.0f},
		.size = 0,
	};
}

