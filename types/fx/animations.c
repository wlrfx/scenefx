#include <bits/time.h>
#include <scenefx/types/wlr_scene.h>

#include "scenefx/types/fx/animations.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

static struct fx_animation_manager {
	struct wl_display *display;
	struct wlr_scene *scene;
} manager;

void fx_animation_manager_init(struct wl_display *display,
							   struct wlr_scene *scene) {
	manager.display = display;
	manager.scene = scene;
}

struct fx_animation_curve {
	double params[4];

	double baked_points_x[FX_ANIMATIONS_BAKED_POINTS_COUNT],
		baked_points_y[FX_ANIMATIONS_BAKED_POINTS_COUNT];
};

struct fx_animation_curve* fx_animation_curve_create(double params[static 4]) {
	struct fx_animation_curve *curve = calloc(1, sizeof(*curve));

	memcpy(curve->params, params, 4 * sizeof(double));

	for (size_t i = 0; i < FX_ANIMATIONS_BAKED_POINTS_COUNT; i++) {
		double t = (double)i / (FX_ANIMATIONS_BAKED_POINTS_COUNT - 1);

		curve->baked_points_x[i] = 3 * t * (1 - t) * (1 - t) * params[0] +
			3 * t * t * (1 - t) * params[2] + t * t * t;

		curve->baked_points_y[i] = 3 * t * (1 - t) * (1 - t) * params[1] +
			3 * t * t * (1 - t) * params[3] + t * t * t;
	}

	return curve;
}

void fx_animation_curve_destroy(struct fx_animation_curve *curve) {
	free(curve);
}

static double find_animation_curve_at(struct fx_animation_curve *curve,
									  double t) {
	size_t down = 0;
	size_t up = FX_ANIMATIONS_BAKED_POINTS_COUNT - 1;

	do {
		size_t middle = (up + down) / 2;
		if (curve->baked_points_x[middle] <= t) {
			down = middle;
		} else {
			up = middle;
		}
	} while (up - down != 1);

	return curve->baked_points_y[up];
}

struct fx_transform_animation {
	struct fx_animation_curve *curve;
	struct wl_event_source *timer;

	struct wlr_box start, end;
	uint32_t time_started, duration;
	uint32_t frame_duration;

	struct wlr_box current;
	bool done;

	fx_transform_animation_callback_func_t callback;
	void *user_data;
};

static uint32_t timespec_to_ms(struct timespec *ts) {
	return (uint32_t)ts->tv_sec * 1000 + (uint32_t)ts->tv_nsec / 1000000;
}

static int timer_animation_update(void *data) {
	struct fx_transform_animation *animation = data;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	uint64_t passed_time = timespec_to_ms(&now) - animation->time_started;
	double progress = (double)passed_time / animation->duration;

	bool done = progress >= 1.0;

	if (done) {
		animation->done = true;
		animation->current = animation->end;
	} else {
		double factor = find_animation_curve_at(animation->curve, progress);

		// interpolate between the start and end boxes
		uint32_t width = animation->start.width +
			(animation->end.width - animation->start.width) * factor;
		uint32_t height = animation->start.height +
			(animation->end.height - animation->start.height) * factor;

		int32_t x = animation->start.x +
			(animation->end.x - animation->start.x) * factor;
		int32_t y = animation->start.y +
			(animation->end.y - animation->start.y) * factor;

		animation->current = (struct wlr_box){x, y, width, height};
	}

	animation->callback(animation->current, animation->done,
					 animation->user_data);

	if (!done) {
		wl_event_source_timer_update(animation->timer,
									 animation->frame_duration);
	}

	return 0;
}

static uint32_t get_fastest_output_refresh_ms(void) {
	struct wlr_scene_output *scene_output;
	uint32_t max = 0;
	wl_list_for_each(scene_output, &manager.scene->outputs, link) {
		struct wlr_output *output = scene_output->output;
		if (output->enabled && (uint32_t)output->refresh > max) {
			max = scene_output->output->refresh;
		}
	}

	// we default to 60 fps
	if (max == 0) {
		max = 60000;
	}

	return 1000000 / max;
}

struct fx_transform_animation* fx_transform_animation_create(
		struct wlr_box start, struct wlr_box end, uint32_t duration,
		struct fx_animation_curve *curve,
		fx_transform_animation_callback_func_t callback, void *user_data) {
	struct fx_transform_animation *animation = calloc(1, sizeof(*animation));

	animation->start = start;
	animation->end = end;

	animation->duration = duration;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	animation->time_started = timespec_to_ms(&ts);

	animation->curve = curve;

	animation->callback = callback;
	animation->user_data = user_data;

	animation->frame_duration = get_fastest_output_refresh_ms();

	animation->current = start;

	callback(animation->current, animation->done, user_data);

	animation->timer =
		wl_event_loop_add_timer(wl_display_get_event_loop(manager.display),
						  timer_animation_update, animation);
	wl_event_source_timer_update(animation->timer, animation->frame_duration);

	return animation;
}

void fx_transform_animation_destroy(struct fx_transform_animation *animation) {
	wl_event_source_remove(animation->timer);
	free(animation);
}

struct wlr_box fx_transform_animation_get_current(
		struct fx_transform_animation *animation) {
	return animation->current;
}

bool fx_transform_animation_is_done(struct fx_transform_animation *animation) {
	return animation->done;
}
