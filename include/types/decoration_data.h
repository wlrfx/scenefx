#ifndef TYPES_DECORATION_DATA
#define TYPES_DECORATION_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>

static float default_shadow_color[] = {0.0f, 0.0f, 0.0f, 0.5f};
static float default_dim_color[] = {0.0f, 0.0f, 0.0f, 1.0f};

struct shadow_data {
	bool enabled;
	float *color;
	float blur_sigma;
};

struct shadow_data shadow_data_get_default(void);

bool shadow_data_is_enabled(struct shadow_data *data);

// TODO: Rename to something like `wlr_scene_node_decoration_data`?
struct decoration_data {
	float alpha;
	float saturation;
	int corner_radius;
	// TODO: Change name from dim to something like "overlay" (maybe replace
	// with it's own struct?)
	float dim;
	float *dim_color;
	// TODO: Change to `enum corner_location` instead?
	bool has_titlebar;
	// TODO: Remove blur bool and add multiple fields (maybe even it's own
	// struct?)
	bool blur;
	struct shadow_data shadow_data;
};

struct decoration_data decoration_data_get_undecorated(void);

#endif
