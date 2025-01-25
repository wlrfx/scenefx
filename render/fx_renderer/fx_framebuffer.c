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

GLuint fx_framebuffer_get_fbo(struct fx_framebuffer *buffer) {
	if (buffer->external_only) {
		wlr_log(WLR_ERROR, "DMA-BUF format is external-only");
		return 0;
	}

	if (buffer->fbo) {
		return buffer->fbo;
	}

	push_fx_debug(buffer->renderer);

	if (!buffer->rbo) {
		glGenRenderbuffers(1, &buffer->rbo);
		glBindRenderbuffer(GL_RENDERBUFFER, buffer->rbo);
		buffer->renderer->procs.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER,
				buffer->image);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
	}

	glGenFramebuffers(1, &buffer->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_RENDERBUFFER, buffer->rbo);
	GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	// Init stencil buffer
	glGenRenderbuffers(1, &buffer->sb);
	glBindRenderbuffer(GL_RENDERBUFFER, buffer->sb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
			buffer->buffer->width, buffer->buffer->height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
			GL_RENDERBUFFER, buffer->sb);
	fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
		wlr_log(WLR_ERROR, "Failed to create FBO");
		glDeleteFramebuffers(1, &buffer->fbo);
		buffer->fbo = 0;
	}

	pop_fx_debug(buffer->renderer);

	return buffer->fbo;
}

void fx_framebuffer_get_or_create_custom(struct fx_renderer *renderer,
		struct wlr_output *output, struct wlr_swapchain *swapchain,
		struct fx_framebuffer **fx_framebuffer, bool *failed) {
	if (*failed) {
		return;
	}

	struct wlr_allocator *allocator = output->allocator;
	if (!swapchain) {
		if (!output->swapchain) {
			wlr_log(WLR_ERROR, "Failed to allocate buffer, no swapchain");
			*failed = true;
			return;
		}
		swapchain = output->swapchain;
	}
	int width = output->width;
	int height = output->height;
	struct wlr_buffer *wlr_buffer = NULL;

	if (*fx_framebuffer == NULL) {
		wlr_buffer = wlr_allocator_create_buffer(allocator, width, height,
				&swapchain->format);
		if (wlr_buffer == NULL) {
			wlr_log(WLR_ERROR, "Failed to allocate buffer");
			*failed = true;
			return;
		}
	} else {
		if ((wlr_buffer = (*fx_framebuffer)->buffer) &&
				wlr_buffer->width == width &&
				wlr_buffer->height == height) {
			return;
		}
		// Create a new wlr_buffer if it's null or if the output size has
		// changed
		fx_framebuffer_destroy(*fx_framebuffer);
		wlr_buffer_drop(wlr_buffer);
		wlr_buffer = wlr_allocator_create_buffer(allocator,
				width, height, &swapchain->format);
	}
	*fx_framebuffer = fx_framebuffer_get_or_create(renderer, wlr_buffer);
	fx_framebuffer_get_fbo(*fx_framebuffer);
}

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

	buffer->image = wlr_egl_create_image_from_dmabuf(renderer->egl,
		&dmabuf, &buffer->external_only);
	if (buffer->image == EGL_NO_IMAGE_KHR) {
		goto error_buffer;
	}

	wlr_addon_init(&buffer->addon, &wlr_buffer->addons, renderer,
		&buffer_addon_impl);

	wl_list_insert(&renderer->buffers, &buffer->link);

	wlr_log(WLR_DEBUG, "Created GL FBO for buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;

error_buffer:
	free(buffer);
	return NULL;
}

void fx_framebuffer_bind(struct fx_framebuffer *fx_buffer) {
	glBindFramebuffer(GL_FRAMEBUFFER, fx_buffer->fbo);
}

void fx_framebuffer_destroy(struct fx_framebuffer *fx_buffer) {
	// Release the framebuffer
	wl_list_remove(&fx_buffer->link);
	wlr_addon_finish(&fx_buffer->addon);

	struct wlr_egl_context prev_ctx;
	wlr_egl_make_current(fx_buffer->renderer->egl, &prev_ctx);

	glDeleteFramebuffers(1, &fx_buffer->fbo);
	fx_buffer->fbo = -1;
	glDeleteRenderbuffers(1, &fx_buffer->rbo);
	fx_buffer->rbo = -1;
	glDeleteTextures(1, &fx_buffer->tex);
	fx_buffer->tex = -1;
	glDeleteRenderbuffers(1, &fx_buffer->sb);
	fx_buffer->sb = -1;

	wlr_egl_destroy_image(fx_buffer->renderer->egl, fx_buffer->image);

	wlr_egl_restore_context(&prev_ctx);

	free(fx_buffer);
}

