#ifndef RENDER_EGL_H
#define RENDER_EGL_H

#include "render/fx_renderer/fx_renderer.h"

/** Make the EGL context current. */
bool egl_ensure_current(struct fx_renderer *fx_renderer);

EGLSyncKHR egl_create_sync(struct fx_renderer *fx_renderer, int fence_fd);
void egl_destroy_sync(struct fx_renderer *fx_renderer, EGLSyncKHR sync);
bool egl_wait_sync(struct fx_renderer *fx_renderer, EGLSyncKHR sync);

#endif
