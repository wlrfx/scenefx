#ifndef FX_FRAMEBUFFER_H
#define FX_FRAMEBUFFER_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <wlr/types/wlr_output.h>

#include "render/fx_renderer/fx_stencilbuffer.h"
#include "render/fx_renderer/fx_texture.h"

struct fx_framebuffer {
	GLuint fb;
	struct fx_stencilbuffer stencil_buffer;
	struct fx_texture texture;
};

struct fx_framebuffer fx_framebuffer_create(void);

void fx_framebuffer_bind(struct fx_framebuffer *buffer);

void fx_framebuffer_update(struct fx_framebuffer *buffer, int width, int height);

void fx_framebuffer_add_stencil_buffer(struct fx_framebuffer *buffer, int width, int height);

void fx_framebuffer_release(struct fx_framebuffer *buffer);

#endif
