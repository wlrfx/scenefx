#include "scenefx/types/fx/animations.h"

#include <stdlib.h>
#include <string.h>

struct fx_animation_curve *
fx_animation_curve_create(double params[static 4]) {
	struct fx_animation_curve *curve = calloc(1, sizeof(*curve));

	memcpy(curve->params, params, 4 * sizeof(double));

	for(size_t i = 0; i < FX_ANIMATIONS_BAKED_POINTS_COUNT; i++) {
		double t = (double)i / (FX_ANIMATIONS_BAKED_POINTS_COUNT - 1);

		curve->baked_points_x[i] =  3 * t * (1 - t) * (1 - t) * params[0]
			+ 3 * t * t * (1 - t) * params[2]
			+ t * t * t;

		curve->baked_points_y[i] = 3 * t * (1 - t) * (1 - t) * params[1]
			+ 3 * t * t * (1 - t) * params[3]
			+ t * t * t;
	}

	return curve;
}

void
fx_animation_curve_destroy(struct fx_animation_curve *curve) {
	free(curve);
}

struct fx_translate_animation *
fx_translate_animation_create(struct wlr_box start, struct wlr_box end, struct fx_animation_curve *curve) {
	struct fx_translate_animation *animation = calloc(1, sizeof(*animation));

	animation->start = start;
	animation->end = end;
	animation->curve = curve;

	return animation;
}

static double
find_animation_curve_at(struct fx_animation_curve *curve, double t) {
	size_t down = 0;
	size_t up = FX_ANIMATIONS_BAKED_POINTS_COUNT - 1;

	do {
		size_t middle = (up + down) / 2;
		if(curve->baked_points_x[middle] <= t) {
			down = middle;
		} else {
			up = middle;
		}
	} while (up - down != 1);

	return curve->baked_points_y[up];
}

struct wlr_box
fx_translate_animation_update(struct fx_translate_animation *animation, double progress) {
	if(progress >= 1.0) {
		animation->finished = true;
		animation->current = animation->end;
		return animation->end;
	}

	double factor = find_animation_curve_at(animation->curve, progress);

	uint32_t width = animation->start.width
		+ (animation->start.width - animation->start.width) * factor;
	uint32_t height = animation->start.height
		+ (animation->start.height - animation->start.height) * factor;

	uint32_t x = animation->start.x
		+ (animation->start.x - animation->start.x) * factor;
	uint32_t y = animation->start.y
		+ (animation->start.y - animation->start.y) * factor;

	animation->current = (struct wlr_box){ x, y, width, height };

	return animation->current;
}

