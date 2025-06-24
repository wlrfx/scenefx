#include <fcntl.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_buffer.h>
#include <xf86drm.h>

#include "render/fx_renderer/util.h"
#include "util/env.h"

static int open_drm_render_node(void) {
	uint32_t flags = 0;
	int devices_len = drmGetDevices2(flags, NULL, 0);
	if (devices_len < 0) {
		wlr_log(WLR_ERROR, "drmGetDevices2 failed: %s", strerror(-devices_len));
		return -1;
	}
	drmDevice **devices = calloc(devices_len, sizeof(*devices));
	if (devices == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return -1;
	}
	devices_len = drmGetDevices2(flags, devices, devices_len);
	if (devices_len < 0) {
		free(devices);
		wlr_log(WLR_ERROR, "drmGetDevices2 failed: %s", strerror(-devices_len));
		return -1;
	}

	int fd = -1;
	for (int i = 0; i < devices_len; i++) {
		drmDevice *dev = devices[i];
		if (dev->available_nodes & (1 << DRM_NODE_RENDER)) {
			const char *name = dev->nodes[DRM_NODE_RENDER];
			wlr_log(WLR_DEBUG, "Opening DRM render node '%s'", name);
			fd = open(name, O_RDWR | O_CLOEXEC);
			if (fd < 0) {
				wlr_log_errno(WLR_ERROR, "Failed to open '%s'", name);
				goto out;
			}
			break;
		}
	}
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to find any DRM render node");
	}

out:
	for (int i = 0; i < devices_len; i++) {
		drmFreeDevice(&devices[i]);
	}
	free(devices);

	return fd;
}

bool open_preferred_drm_fd(struct wlr_backend *backend, int *drm_fd_ptr,
		bool *own_drm_fd) {
	if (*drm_fd_ptr >= 0) {
		return true;
	}

	if (env_parse_bool("WLR_RENDERER_FORCE_SOFTWARE")) {
		*drm_fd_ptr = -1;
		*own_drm_fd = false;
		return true;
	}

	// Allow the user to override the render node
	const char *render_name = getenv("WLR_RENDER_DRM_DEVICE");
	if (render_name != NULL) {
		wlr_log(WLR_INFO,
			"Opening DRM render node '%s' from WLR_RENDER_DRM_DEVICE",
			render_name);
		int drm_fd = open(render_name, O_RDWR | O_CLOEXEC);
		if (drm_fd < 0) {
			wlr_log_errno(WLR_ERROR, "Failed to open '%s'", render_name);
			return false;
		}
		if (drmGetNodeTypeFromFd(drm_fd) != DRM_NODE_RENDER) {
			wlr_log(WLR_ERROR, "'%s' is not a DRM render node", render_name);
			close(drm_fd);
			return false;
		}
		*drm_fd_ptr = drm_fd;
		*own_drm_fd = true;
		return true;
	}

	// Prefer the backend's DRM node, if any
	int backend_drm_fd = wlr_backend_get_drm_fd(backend);
	if (backend_drm_fd >= 0) {
		*drm_fd_ptr = backend_drm_fd;
		*own_drm_fd = false;
		return true;
	}

	// If the backend hasn't picked a DRM FD, but accepts DMA-BUFs, pick an
	// arbitrary render node
	if (backend->buffer_caps & WLR_BUFFER_CAP_DMABUF) {
		int drm_fd = open_drm_render_node();
		if (drm_fd < 0) {
			return false;
		}
		*drm_fd_ptr = drm_fd;
		*own_drm_fd = true;
		return true;
	}

	return false;
}
