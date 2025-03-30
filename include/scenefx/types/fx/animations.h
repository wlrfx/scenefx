#ifndef TYPES_FX_ANIMATIONS_H
#define TYPES_FX_ANIMATIONS_H

#include <stdint.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_shell.h>

#ifndef FX_ANIMATIONS_BAKED_POINTS_COUNT
#define FX_ANIMATIONS_BAKED_POINTS_COUNT 256
#endif

struct fx_animation_curve {
	double params[4];

	double baked_points_x[FX_ANIMATIONS_BAKED_POINTS_COUNT], baked_points_y[FX_ANIMATIONS_BAKED_POINTS_COUNT];
};

struct fx_animation_curve *
fx_animation_curve_create(double params[static 4]);

void
fx_animation_curve_destroy(struct fx_animation_curve *curve);

struct fx_translate_animation {
	struct fx_animation_curve *curve;
	struct wlr_box start, end;

	struct wlr_box current;
	bool finished;
};

struct fx_translate_animation *
fx_translate_animation_create(struct wlr_box start, struct wlr_box end, struct fx_animation_curve *curve);

void
fx_translate_animation_destroy(struct fx_translate_animation *animation);

struct wlr_box
fx_translate_animation_update(struct fx_translate_animation *animation, double progress);

#endif
