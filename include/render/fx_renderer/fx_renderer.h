#ifndef _SWAY_OPENGL_H
#define _SWAY_OPENGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <wlr/render/egl.h>

#include "render/fx_renderer/fx_framebuffer.h"
#include "render/fx_renderer/fx_texture.h"
#include "wlr/util/box.h"

enum corner_location { TOP_LEFT, TOP_RIGHT, BOTTOM_RIGHT, BOTTOM_LEFT, ALL, NONE };

enum fx_tex_shader_source {
	SHADER_SOURCE_TEXTURE_RGBA = 1,
	SHADER_SOURCE_TEXTURE_RGBX = 2,
	SHADER_SOURCE_TEXTURE_EXTERNAL = 3,
};

enum fx_rounded_quad_shader_source {
	SHADER_SOURCE_QUAD_ROUND = 1,
	SHADER_SOURCE_QUAD_ROUND_TOP_LEFT = 2,
	SHADER_SOURCE_QUAD_ROUND_TOP_RIGHT = 3,
	SHADER_SOURCE_QUAD_ROUND_BOTTOM_RIGHT = 4,
	SHADER_SOURCE_QUAD_ROUND_BOTTOM_LEFT = 5,
};

struct decoration_data {
	float alpha;
	float saturation;
	int corner_radius;
	float dim;
	float *dim_color;
	bool has_titlebar;
	bool blur;
	bool shadow;
};

struct quad_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
};

struct rounded_quad_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint size;
	GLint position;
	GLint radius;
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
	GLint saturation;
	GLint dim;
	GLint dim_color;
	GLint has_titlebar;
};

struct fx_renderer {
	float projection[9];

	int viewport_width, viewport_height;

	struct wlr_addon addon;

	struct fx_framebuffer wlr_buffer; // Just the framebuffer used by wlroots
	struct fx_framebuffer main_buffer; // The main FB used for rendering

	struct {
		bool OES_egl_image_external;
	} exts;

	struct {
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
	} procs;

	struct {
		struct quad_shader quad;
		struct rounded_quad_shader rounded_quad;
		struct rounded_quad_shader rounded_tl_quad;
		struct rounded_quad_shader rounded_tr_quad;
		struct rounded_quad_shader rounded_bl_quad;
		struct rounded_quad_shader rounded_br_quad;
		struct tex_shader tex_rgba;
		struct tex_shader tex_rgbx;
		struct tex_shader tex_ext;
	} shaders;
};

struct decoration_data get_undecorated_decoration_data(void);

struct fx_renderer *fx_renderer_create(struct wlr_egl *egl);

void fx_renderer_fini(struct fx_renderer *renderer);

void fx_renderer_begin(struct fx_renderer *renderer, struct wlr_output *output,
		int width, int height);

void fx_renderer_clear(const float color[static 4]);

void fx_renderer_scissor(struct wlr_box *box);

bool fx_render_subtexture_with_matrix(struct fx_renderer *renderer, struct fx_texture *fx_texture,
		const struct wlr_fbox *src_box, const struct wlr_box *dst_box, const float matrix[static 9],
		struct decoration_data deco_data);

bool fx_render_texture_with_matrix(struct fx_renderer *renderer, struct fx_texture *fx_texture,
		const struct wlr_box *dst_box, const float matrix[static 9], struct decoration_data deco_data);

void fx_render_rect(struct fx_renderer *renderer, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]);

void fx_render_rounded_rect(struct fx_renderer *renderer, const struct wlr_box *box,
		const float color[static 4], const float matrix[static 9], int radius,
		enum corner_location corner_location);

#endif
