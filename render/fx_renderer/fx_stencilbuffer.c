#include <assert.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include "include/render/fx_renderer/fx_stencilbuffer.h"

struct fx_stencilbuffer fx_stencilbuffer_create(void) {
	return (struct fx_stencilbuffer) {
		.rb = -1,
		.linked_fbo = -1,
		.width = -1,
		.height = -1,
	};
}

void fx_stencilbuffer_init(struct fx_stencilbuffer *stencil_buffer, GLuint fbo,
		int width, int height) {
	bool first_alloc = false;

	if (stencil_buffer->rb == (uint32_t) -1) {
		glGenRenderbuffers(1, &stencil_buffer->rb);
		first_alloc = true;
	}

	if (stencil_buffer->linked_fbo != fbo ||
			stencil_buffer->linked_fbo == (uint32_t) -1) {
		stencil_buffer->linked_fbo = fbo;
		first_alloc = true;
	}

	if (first_alloc || stencil_buffer->width != width || stencil_buffer->height != height) {
		glBindRenderbuffer(GL_RENDERBUFFER, stencil_buffer->rb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
				GL_RENDERBUFFER, stencil_buffer->rb);
		stencil_buffer->width = width;
		stencil_buffer->height = height;

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			wlr_log(WLR_ERROR, "Stencil buffer incomplete, couldn't create! (FB status: %i)", status);
			return;
		}
	}
}

void fx_stencilbuffer_release(struct fx_stencilbuffer *stencil_buffer) {
	if (stencil_buffer->rb != (uint32_t) -1 && stencil_buffer->rb) {
		glDeleteRenderbuffers(1, &stencil_buffer->rb);
	}
	stencil_buffer->rb = -1;
	stencil_buffer->linked_fbo = -1;
	stencil_buffer->width = -1;
	stencil_buffer->height = -1;
}
