#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/vulkan.h>
#include <wlr/util/log.h>

#include "render/vulkan/vulkan.h"

void vk_buffer_destroy(struct vk_buffer *buffer) {
	if (!buffer) {
		wlr_log(WLR_ERROR, "Trying to destroy an already destroyed vk_buffer");
		return;
	}

	// Release the framebuffer
	wl_list_remove(&buffer->link);
	wlr_addon_finish(&buffer->addon);

	// TODO:
	// if (buffer->sb != 0) {
	// 	egl_ensure_current(buffer->vk_renderer);
	// 	glDeleteRenderbuffers(1, &buffer->sb);
	// 	buffer->sb = 0;
	// }

	free(buffer);
}

static void handle_buffer_destroy(struct wlr_addon *addon) {
	struct vk_buffer *buffer = wl_container_of(addon, buffer, addon);
	vk_buffer_destroy(buffer);
}

static const struct wlr_addon_interface buffer_addon_impl = {
	.name = "vk_buffer",
	.destroy = handle_buffer_destroy,
};

static struct vk_buffer *vk_buffer_init(struct fx_renderer *fx_renderer,
		struct wlr_buffer *wlr_buffer, bool needs_stencil) {
	struct vk_renderer *vk_renderer = vk_get_renderer(fx_renderer);

	struct vk_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "vk_framebuffer allocation failed");
		return NULL;
	}
	buffer->vk_renderer = vk_renderer;
	buffer->wlr_buffer = wlr_buffer;

	// TODO:
	// buffer->fbo = wlr_vk_renderer_get_buffer_fbo(fx_renderer->wlr_renderer, wlr_buffer);
	// if (!buffer->fbo) {
	// 	free(buffer);
	// 	wlr_log_errno(WLR_ERROR, "Could not get wlr_buffer fbo for vk_framebuffer");
	// 	return NULL;
	// }

	// TODO:
	// Init stencil buffer if needed
	// buffer->sb = 0;
	// if (needs_stencil) {
	// 	glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
	// 	glGenRenderbuffers(1, &buffer->sb);
	// 	glBindRenderbuffer(GL_RENDERBUFFER, buffer->sb);
	// 	glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
	// 			wlr_buffer->width, wlr_buffer->height);
	// 	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	// 	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
	// 			GL_RENDERBUFFER, buffer->sb);
	// 	GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	// 	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	//
	// 	if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
	// 		wlr_log(WLR_ERROR, "Failed to create vk_buffer stencil");
	// 		glDeleteRenderbuffers(1, &buffer->sb);
	// 		free(buffer);
	// 		return NULL;
	// 	}
	// }

	wl_list_insert(&vk_renderer->buffers, &buffer->link);
	wlr_addon_init(&buffer->addon, &wlr_buffer->addons, fx_renderer,
		&buffer_addon_impl);

	return buffer;
}

struct vk_buffer *vk_buffer_get_or_create(struct fx_renderer *fx_renderer,
		struct wlr_buffer *wlr_buffer, bool needs_stencil) {
	struct wlr_addon *addon =
		wlr_addon_find(&wlr_buffer->addons, fx_renderer, &buffer_addon_impl);
	if (addon) {
		struct vk_buffer *buffer = wl_container_of(addon, buffer, addon);
		return buffer;
	}

	struct vk_buffer *buffer = vk_buffer_init(fx_renderer, wlr_buffer, needs_stencil);
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Failed to create fx_framebuffer from impl");
		return NULL;
	}

	wlr_log(WLR_DEBUG, "Created GL FBO for buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;
}

void vk_buffer_get_or_allocate(struct fx_renderer *fx_renderer,
		struct wlr_allocator *allocator, int width, int height, bool has_alpha,
		struct vk_buffer **vk_buffer, bool *failed) {
	if (*failed) {
		return;
	}

	if (*vk_buffer != NULL) {
		struct wlr_buffer *wlr_buffer = (*vk_buffer)->wlr_buffer;
		if (wlr_buffer != NULL) {
			if (wlr_buffer->width == width && wlr_buffer->height == height) {
				return;
			}
			// Create a new wlr_buffer if it's null or if the output size has
			// changed
			wlr_buffer_drop(wlr_buffer);
		} else {
			vk_buffer_destroy(*vk_buffer);
		}
		*vk_buffer = NULL;
	}

	struct wlr_renderer *wlr_renderer = fx_renderer->wlr_renderer;

	const struct wlr_drm_format *format = find_wlr_drm_format(wlr_renderer, has_alpha);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to get a supported texture format while allocating buffer");
		*failed = true;
		return;
	}

	struct wlr_buffer *wlr_buffer = wlr_allocator_create_buffer(allocator, width, height, format);
	if (wlr_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_buffer");
		*failed = true;
		return;
	}

	*vk_buffer = vk_buffer_get_or_create(fx_renderer, wlr_buffer, false);
	if (*vk_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate fx_buffer");
		wlr_buffer_drop(wlr_buffer);
		*failed = true;
		return;
	}
}
