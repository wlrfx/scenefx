#ifndef _FX_OPENGL_H
#define _FX_OPENGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <wlr/render/egl.h>

#include "wlr/util/box.h"

enum fx_tex_shader_source {
	SHADER_SOURCE_TEXTURE_RGBA = 1,
	SHADER_SOURCE_TEXTURE_RGBX = 2,
	SHADER_SOURCE_TEXTURE_EXTERNAL = 3,
};

struct decoration_data {
	float alpha;
	int corner_radius;
};

struct quad_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
};

struct tex_shader {
	GLuint program;
	GLint proj;
	GLint tex;
	GLint alpha;
	GLint pos_attrib;
	GLint tex_attrib;
	GLint size;
	GLint position;
	GLint radius;
};

struct fx_renderer {
	float projection[9];

	struct wlr_addon addon;

	struct {
		bool OES_egl_image_external;
	} exts;

	struct {
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
	} procs;

	struct {
		struct quad_shader quad;
		struct tex_shader tex_rgba;
		struct tex_shader tex_rgbx;
		struct tex_shader tex_ext;
	} shaders;
};

struct fx_renderer *fx_renderer_create(struct wlr_egl *egl);

void fx_renderer_fini(struct fx_renderer *renderer);

void fx_renderer_begin(struct fx_renderer *renderer, struct wlr_output *output);

void fx_renderer_clear(const float color[static 4]);

void fx_renderer_scissor(struct wlr_box *box);

bool fx_render_subtexture_with_matrix(struct fx_renderer *renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *src_box, const struct wlr_box *dst_box, const float matrix[static 9],
		struct decoration_data deco_data);

bool fx_render_texture_with_matrix(struct fx_renderer *renderer, struct wlr_texture *texture,
		const struct wlr_box *dst_box, const float matrix[static 9], struct decoration_data deco_data);

void fx_render_rect(struct fx_renderer *renderer, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]);

#endif
