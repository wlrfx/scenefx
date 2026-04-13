#ifndef SCENEFX_SCENEFX_H
#define SCENEFX_SCENEFX_H

#include <wlr/render/wlr_renderer.h>

#include "types/wlr_scene.h"

struct fx_renderer;

/** Initialize the SceneFX FX Renderer */
struct fx_renderer *scenefx_init_complete(struct wlr_scene *scene, struct wlr_backend *backend);
/** Initialize the SceneFX FX Renderer. A wrapper for scenefx_init_complete(...) */
struct wlr_renderer *scenefx_init(struct wlr_scene *scene, struct wlr_backend *backend);
struct wlr_renderer *fx_renderer_get_wlr_renderer(struct fx_renderer *fx_renderer);

struct fx_renderer *scenefx_find_fx_renderer(struct wlr_scene *scene,
		struct wlr_renderer *wlr_renderer);

#endif
