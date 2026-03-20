#include <EGL/egl.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>

#include "render/egl.h"

static bool egl_is_current(struct fx_renderer *fx_renderer) {
	return eglGetCurrentContext() == wlr_egl_get_context(fx_renderer->wlr_egl);
}

static bool egl_make_current(struct fx_renderer *fx_renderer) {
	if (!eglMakeCurrent(wlr_egl_get_display(fx_renderer->wlr_egl), EGL_NO_SURFACE, EGL_NO_SURFACE,
			wlr_egl_get_context(fx_renderer->wlr_egl))) {
		wlr_log(WLR_ERROR, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

bool egl_ensure_current(struct fx_renderer *fx_renderer) {
	if (!egl_is_current(fx_renderer)) {
		return egl_make_current(fx_renderer);
	}
	return true;
}

EGLSyncKHR egl_create_sync(struct fx_renderer *fx_renderer, int fence_fd) {
	if (!fx_renderer->egl_procs.eglCreateSyncKHR) {
		return EGL_NO_SYNC_KHR;
	}

	EGLint attribs[3] = { EGL_NONE };
	int dup_fd = -1;
	if (fence_fd >= 0) {
		dup_fd = fcntl(fence_fd, F_DUPFD_CLOEXEC, 0);
		if (dup_fd < 0) {
			wlr_log_errno(WLR_ERROR, "dup failed");
			return EGL_NO_SYNC_KHR;
		}

		attribs[0] = EGL_SYNC_NATIVE_FENCE_FD_ANDROID;
		attribs[1] = dup_fd;
		attribs[2] = EGL_NONE;
	}

	EGLSyncKHR sync = fx_renderer->egl_procs.eglCreateSyncKHR(wlr_egl_get_display(fx_renderer->wlr_egl),
		EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);
	if (sync == EGL_NO_SYNC_KHR) {
		wlr_log(WLR_ERROR, "eglCreateSyncKHR failed");
		if (dup_fd >= 0) {
			close(dup_fd);
		}
	}
	return sync;
}

void egl_destroy_sync(struct fx_renderer *fx_renderer, EGLSyncKHR sync) {
	if (sync == EGL_NO_SYNC_KHR) {
		return;
	}
	assert(fx_renderer->egl_procs.eglDestroySyncKHR);
	if (fx_renderer->egl_procs.eglDestroySyncKHR(wlr_egl_get_display(fx_renderer->wlr_egl), sync) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "eglDestroySyncKHR failed");
	}
}

bool egl_wait_sync(struct fx_renderer *fx_renderer, EGLSyncKHR sync) {
	if (fx_renderer->egl_procs.eglWaitSyncKHR(wlr_egl_get_display(fx_renderer->wlr_egl), sync, 0) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "eglWaitSyncKHR failed");
		return false;
	}
	return true;
}
