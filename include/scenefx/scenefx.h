#ifndef SCENEFX_SCENEFX_H
#define SCENEFX_SCENEFX_H

#include <wlr/render/wlr_renderer.h>

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
struct wlr_renderer *scenefx_renderer_init(struct wlr_backend *backend);

/**
 * Initializes the SceneFX FX Renderer from an existing wlr_renderer.
 *
 * Returns `NULL` if the FX Renderer initialization fails.
 */
struct fx_renderer *scenefx_renderer_init_from_wlr(struct wlr_renderer *wlr_renderer);

/**
 * Try getting the attached FX Renderer from a wlr_renderer.
 *
 * Returns `NULL` if an attached FX Renderer wasn't found.
 */
struct fx_renderer *fx_renderer_try_from_wlr(struct wlr_renderer *wlr_renderer);

#endif
