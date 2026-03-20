#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
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

struct fx_framebuffer *fx_framebuffer_get_or_create(struct fx_renderer *fx_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_addon *addon =
		wlr_addon_find(&wlr_buffer->addons, fx_renderer->wlr_renderer, &buffer_addon_impl);
	if (addon) {
		struct fx_framebuffer *buffer = wl_container_of(addon, buffer, addon);
		return buffer;
	}

	struct fx_framebuffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	buffer->wlr_buffer = wlr_buffer;
	buffer->fx_renderer = fx_renderer;

	// Init the base wlr_gles2_buffer
	GLuint fbo = wlr_gles2_renderer_get_buffer_fbo(fx_renderer->wlr_renderer, wlr_buffer);
	if (!fbo) {
		free(buffer);
		wlr_log_errno(WLR_ERROR, "Could not get wlr_buffer fbo");
		return NULL;
	}
	buffer->fbo = fbo;

	// Init stencil buffer
	glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
	glGenRenderbuffers(1, &buffer->sb);
	glBindRenderbuffer(GL_RENDERBUFFER, buffer->sb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
			wlr_buffer->width, wlr_buffer->height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
			GL_RENDERBUFFER, buffer->sb);
	GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
		wlr_log(WLR_ERROR, "Failed to create FBO");
		free(buffer);
		return NULL;
	}

	wlr_addon_init(&buffer->addon, &wlr_buffer->addons, fx_renderer->wlr_renderer,
		&buffer_addon_impl);

	wl_list_insert(&fx_renderer->buffers, &buffer->link);

	wlr_log(WLR_DEBUG, "Created GL FBO for buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;
}

void fx_framebuffer_get_or_create_custom(struct fx_renderer *fx_renderer,
		struct wlr_allocator *allocator, int width, int height, bool has_alpha,
		struct fx_framebuffer **fx_framebuffer, bool *failed) {
	if (*failed) {
		return;
	}

	if (*fx_framebuffer != NULL) {
		struct wlr_buffer *wlr_buffer = (*fx_framebuffer)->wlr_buffer;
		if (wlr_buffer != NULL) {
			if (wlr_buffer->width == width && wlr_buffer->height == height) {
				return;
			}
			// Create a new wlr_buffer if it's null or if the output size has
			// changed
			wlr_buffer_drop(wlr_buffer);
		} else {
			fx_framebuffer_destroy(*fx_framebuffer);
		}
		*fx_framebuffer = NULL;
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

	*fx_framebuffer = fx_framebuffer_get_or_create(fx_renderer, wlr_buffer);
	if (*fx_framebuffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate fx_buffer");
		wlr_buffer_drop(wlr_buffer);
		*failed = true;
		return;
	}
}

void fx_framebuffer_bind(struct fx_framebuffer *fx_buffer) {
	glBindFramebuffer(GL_FRAMEBUFFER, fx_buffer->fbo);
}

void fx_framebuffer_destroy(struct fx_framebuffer *fx_buffer) {
	if (!fx_buffer) {
		wlr_log(WLR_ERROR, "Trying to destroy an already destroyed fx_framebuffer");
		return;
	}

	// Release the framebuffer
	wl_list_remove(&fx_buffer->link);
	wlr_addon_finish(&fx_buffer->addon);

	egl_ensure_current(fx_buffer->fx_renderer);
	glDeleteRenderbuffers(1, &fx_buffer->sb);
	fx_buffer->sb = 0;

	free(fx_buffer);
}

