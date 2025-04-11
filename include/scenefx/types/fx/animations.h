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

/* initializes the animation system.
 * compositors should call this before any of the calls to later functions */
void
fx_animation_manager_init(struct wl_display *display, struct wlr_scene *scene);

/* bezier curve that will dictate the animation's progress */
struct fx_animation_curve;

struct fx_animation_curve *
fx_animation_curve_create(double params[static 4]);

void
fx_animation_curve_destroy(struct fx_animation_curve *curve);

typedef void (*fx_transform_animation_callback_func_t)(struct wlr_box current, bool done, void *user_data);

/* tranform animations will transform the `start` box to the `end` box,
 * calling `callback` on regular intervals with the current box contained in `current` */
struct fx_transform_animation;

/* creates a new transform animation */
struct fx_transform_animation *
fx_transform_animation_create(struct wlr_box start, struct wlr_box end, uint32_t duration,
        struct fx_animation_curve *curve, fx_transform_animation_callback_func_t callback, void *user_data);

/* a user may destroy the animation at any momment, stopping the timer callbacks and freeing the resources.
 * you probably want to call this in the `callback` when the `done` flag is set to `true` */
void
fx_transform_animation_destroy(struct fx_transform_animation *animation);

/* get the current progress of the `animation` */
struct wlr_box
fx_transform_animation_get_current(struct fx_transform_animation *animation);

/* get if the animation is done */
bool
fx_transform_animation_is_done(struct fx_transform_animation *animation);
#endif
