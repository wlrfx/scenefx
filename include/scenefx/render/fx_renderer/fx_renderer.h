#ifndef SCENEFX_FX_OPENGL_H
#define SCENEFX_FX_OPENGL_H

#include <GLES2/gl2.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_buffer.h>

struct wlr_renderer *fx_renderer_create_with_drm_fd(int drm_fd);
struct wlr_renderer *fx_renderer_create(struct wlr_backend *backend);

struct fx_renderer *fx_get_renderer(
	struct wlr_renderer *wlr_renderer);

//
// fx_texture
//

struct fx_texture_attribs {
	GLenum target; /* either GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES */
	GLuint tex;

	bool has_alpha;
};

struct wlr_texture *fx_texture_from_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *buffer);

void fx_texture_get_attribs(struct wlr_texture *texture,
	struct fx_texture_attribs *attribs);

#endif
