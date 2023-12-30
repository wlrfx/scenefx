#include <stdio.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/interface.h>
#include <wlr/render/swapchain.h>
#include <wlr/util/log.h>

#include "render/egl.h"
#include "render/fx_renderer/fx_renderer.h"

static void handle_buffer_destroy(struct wlr_addon *addon) {
	struct fx_framebuffer *buffer =
		wl_container_of(addon, buffer, addon);
	fx_framebuffer_destroy(buffer);
}

static const struct wlr_addon_interface buffer_addon_impl = {
	.name = "fx_framebuffer",
	.destroy = handle_buffer_destroy,
};

struct fx_framebuffer *fx_framebuffer_get_or_create(struct fx_renderer *renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_addon *addon =
		wlr_addon_find(&wlr_buffer->addons, renderer, &buffer_addon_impl);
	if (addon) {
		struct fx_framebuffer *buffer = wl_container_of(addon, buffer, addon);
		return buffer;
	}

	struct fx_framebuffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	buffer->buffer = wlr_buffer;
	buffer->renderer = renderer;

	struct wlr_dmabuf_attributes dmabuf = {0};
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &dmabuf)) {
		goto error_buffer;
	}

	bool external_only;
	buffer->image = wlr_egl_create_image_from_dmabuf(renderer->egl,
		&dmabuf, &external_only);
	if (buffer->image == EGL_NO_IMAGE_KHR) {
		goto error_buffer;
	}

	push_fx_debug(renderer);

	glGenRenderbuffers(1, &buffer->rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, buffer->rbo);
	renderer->procs.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER,
		buffer->image);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &buffer->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, buffer->rbo);
	GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	pop_fx_debug(renderer);

	if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
		wlr_log(WLR_ERROR, "Failed to create FBO");
		goto error_image;
	}

	wlr_addon_init(&buffer->addon, &wlr_buffer->addons, renderer,
		&buffer_addon_impl);

	wl_list_insert(&renderer->buffers, &buffer->link);

	wlr_log(WLR_DEBUG, "Created GL FBO for buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;

error_image:
	wlr_egl_destroy_image(renderer->egl, buffer->image);
error_buffer:
	free(buffer);
	return NULL;
}

void fx_framebuffer_bind(struct fx_framebuffer *fx_buffer) {
	glBindFramebuffer(GL_FRAMEBUFFER, fx_buffer->fbo);
}

void fx_framebuffer_bind_wlr_fbo(struct fx_renderer *renderer) {
	glBindFramebuffer(GL_FRAMEBUFFER, renderer->current_buffer->fbo);
}

void fx_framebuffer_destroy(struct fx_framebuffer *fx_buffer) {
	// Release the framebuffer
	wl_list_remove(&fx_buffer->link);
	wlr_addon_finish(&fx_buffer->addon);

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(fx_buffer->renderer->egl);

	glDeleteFramebuffers(1, &fx_buffer->fbo);
	fx_buffer->fbo = -1;
	glDeleteRenderbuffers(1, &fx_buffer->rbo);
	fx_buffer->rbo = -1;

	wlr_egl_destroy_image(fx_buffer->renderer->egl, fx_buffer->image);

	wlr_egl_restore_context(&prev_ctx);

	free(fx_buffer);
}
