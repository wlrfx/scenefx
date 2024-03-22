#ifndef TYPES_DECORATION_DATA
#define TYPES_DECORATION_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>
#include <wlr/render/pass.h>

struct shadow_data {
	bool enabled;
	struct wlr_render_color color;
	float blur_sigma;
	float offset_x;
	float offset_y;
};

struct shadow_data shadow_data_get_default(void);

bool scene_buffer_has_shadow(struct shadow_data *data);

#endif
