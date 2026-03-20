#ifndef SCENEFX_SCENEFX_H
#define SCENEFX_SCENEFX_H

#include <wlr/render/wlr_renderer.h>

#include "types/wlr_scene.h"

struct wlr_renderer *scenefx_init(struct wlr_scene *scene, struct wlr_backend *backend);

#endif
