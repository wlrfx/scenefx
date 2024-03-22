#ifndef SCENEFX_FX_OPENGL_H
#define SCENEFX_FX_OPENGL_H

#include <wlr/backend.h>

struct wlr_renderer *fx_renderer_create_with_drm_fd(int drm_fd);
struct wlr_renderer *fx_renderer_create(struct wlr_backend *backend);

struct fx_renderer *fx_get_renderer(
	struct wlr_renderer *wlr_renderer);

#endif
