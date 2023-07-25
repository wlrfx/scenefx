#ifndef TYPES_DECORATION_DATA
#define TYPES_DECORATION_DATA

#include <stdbool.h>
#include <wlr/util/addon.h>

static float default_shadow_color[] = {0.0f, 0.0f, 0.0f, 0.5f};
static float default_dim_color[] = {0.0f, 0.0f, 0.0f, 1.0f};

// TODO: Rename to something like `wlr_scene_node_decoration_data`?
struct decoration_data {
	float alpha;
	int corner_radius;
};

struct decoration_data decoration_data_get_undecorated(void);

#endif
