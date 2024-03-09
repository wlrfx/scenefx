#ifndef TYPES_SHADOW_DATA_H
#define TYPES_SHADOW_DATA_H

#include <stdbool.h>
#include <wlr/util/addon.h>
#include <wlr/render/pass.h>

struct shadow_data {
	bool enabled;
	struct wlr_render_color color;
	float blur_sigma;
};

struct shadow_data shadow_data_get_default(void);

bool scene_buffer_has_shadow(struct shadow_data *data);

#endif
