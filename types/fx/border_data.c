#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "types/fx/border_data.h"
#include "wlr/util/log.h"

struct border_data border_data_get_default(void) {
	static float default_border_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
	struct border_data data = {
		.enabled = false,
		.width = 0,
		.height = 0,
		.x_offset = 0,
		.y_offset = 0,
	};
	memcpy(&data.color, &default_border_color, sizeof(data.color));
	return data;
}

bool scene_buffer_has_border(struct border_data *data) {
	return data->enabled &&
		(data->width + data->height + data->x_offset + data->y_offset) > 0 &&
		data->color[3] > 0.0;
}

int border_data_get_min_size(struct border_data *data) {
	return data->width < data->height ? data->width : data->height;
}

/** Expand the damaged region by the border size, compensating for x/y offset */
void border_data_expand_damage(struct border_data *data, pixman_region32_t *damage) {
	if (data->height == 0 && data->width == 0 &&
			data->x_offset == 0 && data->y_offset == 0) {
		return;
	}

	int nrects;
	pixman_box32_t *src_rects = pixman_region32_rectangles(damage, &nrects);

	pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
	if (dst_rects == NULL) {
		return;
	}

	for (int i = 0; i < nrects; ++i) {
		dst_rects[i].x1 = src_rects[i].x1 - data->width + data->x_offset;
		dst_rects[i].x2 = src_rects[i].x2 + data->width + data->x_offset;
		dst_rects[i].y1 = src_rects[i].y1 - data->height + data->y_offset;
		dst_rects[i].y2 = src_rects[i].y2 + data->height + data->y_offset;
	}

	pixman_region32_fini(damage);
	pixman_region32_init_rects(damage, dst_rects, nrects);
	free(dst_rects);
}
