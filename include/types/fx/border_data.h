#ifndef TYPES_BORDER_DATA
#define TYPES_BORDER_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>
#include <pixman.h>

struct border_data {
	bool enabled;
	int width;
	int height;
	int x_offset;
	int y_offset;
	float color[4];
};

struct border_data border_data_get_default(void);

bool scene_buffer_has_border(struct border_data *data);

int border_data_get_min_size(struct border_data *data);

void border_data_expand_damage(struct border_data *data, pixman_region32_t *damage);

#endif
