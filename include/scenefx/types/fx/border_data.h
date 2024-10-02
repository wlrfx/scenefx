#ifndef TYPES_BORDER_DECORATION_DATA
#define TYPES_BORDER_DECORATION_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>
#include <wlr/render/pass.h>

struct border_data {
	int size;
	struct wlr_render_color color;
};

struct border_data border_data_get_default(void);

#endif
