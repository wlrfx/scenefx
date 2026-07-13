#ifndef SCENEFX_SCENEFX_H
#define SCENEFX_SCENEFX_H

#include <wlr/render/wlr_renderer.h>

#include "types/wlr_scene.h"

struct fx_renderer;

/**
 * Initialize the SceneFX FX Renderer and creates a new wlr_renderer.
 *
 * Creates a standard `struct wlr_renderer *` just like `wlr_renderer_autocreate(...)`
 * but also conditionally creates an internal FX Renderer.
 *
 * Returns `NULL` if initializing a standard wlr_renderer fails, just like
 * `wlr_renderer_autocreate(...)`. Returns a valid `non-NULL` wlr_renderer
 * even if the FX Renderer initialization fails.
 */
struct wlr_renderer *scenefx_init(struct wlr_scene *wlr_scene, struct wlr_backend *backend);

/**
 * Initialize the SceneFX FX Renderer and creates a new wlr_renderer.
 * It's preferred to use `scenefx_init(...)` instead if full control over the FX
 * Renderer isn't needed.
 *
 * Returns `NULL` if the FX Renderer initialization fails.
 */
struct fx_renderer *scenefx_init_complete(struct wlr_scene *wlr_scene,
		struct wlr_backend *backend);

/**
 * Initializes the SceneFX FX Renderer from an existing wlr_renderer.
 *
 * Returns `NULL` if the FX Renderer initialization fails.
 */
struct fx_renderer *scenefx_init_from_wlr_renderer(struct wlr_renderer *wlr_renderer,
		struct wlr_scene *wlr_scene);

/** Get the internal wlr_renderer from an FX Renderer */
struct wlr_renderer *fx_renderer_get_wlr_renderer(struct fx_renderer *fx_renderer);

/** Try getting the FX Renderer from the wlr_scene addons list. */
struct fx_renderer *scenefx_find_fx_renderer(struct wlr_scene *scene,
		struct wlr_renderer *wlr_renderer);

#endif
