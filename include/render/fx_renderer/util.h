#ifndef _FX_UTIL_H
#define _FX_UTIL_H

#include <stdbool.h>
#include <stdlib.h>
#include <wlr/backend/interface.h>

bool open_preferred_drm_fd(struct wlr_backend *backend, int *drm_fd_ptr,
		bool *own_drm_fd);

#endif
