#ifndef _FX_OPENGL_H
#define _FX_OPENGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>
#include "render/fx_renderer/fx_stencilbuffer.h"
#include "types/fx/shadow_data.h"

enum fx_tex_shader_source {
	SHADER_SOURCE_TEXTURE_RGBA = 1,
	SHADER_SOURCE_TEXTURE_RGBX = 2,
	SHADER_SOURCE_TEXTURE_EXTERNAL = 3,
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

struct stencil_mask_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint half_size;
	GLint position;
	GLint radius;
};

struct box_shadow_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint position;
	GLint size;
	GLint blur_sigma;
	GLint corner_radius;
};

struct fx_renderer {
	float projection[9];

	struct fx_stencilbuffer stencil_buffer;

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
		struct box_shadow_shader box_shadow;
		struct stencil_mask_shader stencil_mask;
	} shaders;
};

void fx_renderer_init_addon(struct wlr_egl *egl, struct wlr_addon_set *addons,
		const void * owner);

struct fx_renderer *fx_renderer_addon_find(struct wlr_addon_set *addons,
		const void * owner);

struct fx_renderer *fx_renderer_create(struct wlr_egl *egl);

void fx_renderer_fini(struct fx_renderer *renderer);

void fx_renderer_begin(struct fx_renderer *renderer, struct wlr_output *output);

void fx_renderer_clear(const float color[static 4]);

void fx_renderer_scissor(struct wlr_box *box);

// Initialize the stenciling work
void fx_renderer_stencil_mask_init(void);

// Close the mask
void fx_renderer_stencil_mask_close(bool draw_inside_mask);

// Finish stenciling and clear the buffer
void fx_renderer_stencil_mask_fini(void);

void fx_renderer_stencil_enable(void);

void fx_renderer_stencil_disable(void);

bool fx_render_subtexture_with_matrix(struct fx_renderer *renderer,
		struct wlr_texture *wlr_texture, const struct wlr_fbox *src_box,
		const struct wlr_box *dst_box, const float matrix[static 9],
		float opacity, int corner_radius);

void fx_render_rect(struct fx_renderer *renderer, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]);

void fx_render_box_shadow(struct fx_renderer *renderer,
		const struct wlr_box *box, const struct wlr_box *stencil_box,
		const float matrix[static 9], int corner_radius,
		struct shadow_data *shadow_data);

#endif
