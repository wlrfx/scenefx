#ifndef TYPES_FX_ANIMATIONS_H
#define TYPES_FX_ANIMATIONS_H

#include "wlr/util/box.h"
#include <scenefx/types/wlr_scene.h>

#include <stdint.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_shell.h>

#ifndef FX_ANIMATIONS_BAKED_POINTS_COUNT
#define FX_ANIMATIONS_BAKED_POINTS_COUNT 256
#endif

void
fx_animation_manager_init(struct wl_display *display, struct wlr_scene *scene);

struct fx_animation_curve {
	double params[4];

	double baked_points_x[FX_ANIMATIONS_BAKED_POINTS_COUNT], baked_points_y[FX_ANIMATIONS_BAKED_POINTS_COUNT];
};

struct fx_animation_curve *
fx_animation_curve_create(double params[static 4]);

void
fx_animation_curve_destroy(struct fx_animation_curve *curve);

typedef void (*fx_transform_animation_callback_func_t)(struct wlr_box current, bool done, void *user_data);

struct fx_transform_animation;

struct fx_transform_animation *
fx_transform_animation_create(struct wlr_box start, struct wlr_box end, uint32_t duration,
        struct fx_animation_curve *curve, fx_transform_animation_callback_func_t callback, void *user_data);

void
fx_transform_animation_destroy(struct fx_transform_animation *animation);

struct wlr_box
fx_transform_animation_get_current(struct fx_transform_animation *animation);

bool
fx_transform_animation_is_done(struct fx_transform_animation *animation);

#endif
