#ifndef _FX_OPENGL_H
#define _FX_OPENGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_texture.h>
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
struct blur_shader {
	GLuint program;
	GLint proj;
	GLint tex;
	GLint pos_attrib;
	GLint tex_attrib;
	GLint radius;
	GLint halfpixel;
};

struct fx_framebuffer {
	bool initialized;

	GLuint fbo;
	GLuint rbo;

	struct wlr_buffer *wlr_buffer;
	struct fx_renderer *renderer;
	struct wl_list link; // fx_renderer.buffers
	struct wlr_addon addon;

	EGLImageKHR image;
};

struct fx_texture {
	struct wlr_texture wlr_texture;
	struct fx_renderer *fx_renderer;
	struct wl_list link; // fx_renderer.textures

	// Basically:
	//   GL_TEXTURE_2D == mutable
	//   GL_TEXTURE_EXTERNAL_OES == immutable
	GLuint target;
	GLuint tex;

	EGLImageKHR image;

	bool has_alpha;

	// Only affects target == GL_TEXTURE_2D
	uint32_t drm_format; // used to interpret upload data
	// If imported from a wlr_buffer
	struct wlr_buffer *buffer;
	struct wlr_addon buffer_addon;
};

struct fx_renderer {
	float projection[9];

	int viewport_width, viewport_height;

	struct wlr_output *wlr_output;

	struct wlr_egl *egl;

	struct fx_stencilbuffer stencil_buffer;

	struct wl_list textures; // fx_texture.link
	struct wl_list buffers; // fx_framebuffer.link

	// The FBO and texture used by wlroots
	GLuint wlr_main_buffer_fbo;
	struct wlr_gles2_texture_attribs wlr_main_texture_attribs;

	// Contains the blurred background for tiled windows
	// struct wlr_buffer blur_buffer;
	struct fx_framebuffer blur_buffer;
	// Contains the original pixels to draw over the areas where artifact are visible
	struct fx_framebuffer blur_saved_pixels_buffer;
	// Blur swaps between the two effects buffers everytime it scales the image
	// Buffer used for effects
	struct fx_framebuffer effects_buffer;
	// Swap buffer used for effects
	struct fx_framebuffer effects_buffer_swapped;

	// The region where there's blur
	pixman_region32_t blur_padding_region;

	bool blur_buffer_dirty;

	struct wlr_addon addon;

	struct {
		bool OES_egl_image_external;
		bool OES_egl_image;
	} exts;

	struct {
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
		PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES;
	} procs;

	struct {
		struct quad_shader quad;
		struct tex_shader tex_rgba;
		struct tex_shader tex_rgbx;
		struct tex_shader tex_ext;
		struct box_shadow_shader box_shadow;
		struct stencil_mask_shader stencil_mask;
		struct blur_shader blur1;
		struct blur_shader blur2;
	} shaders;
};

///
/// fx_framebuffer
///

struct fx_framebuffer fx_framebuffer_create(void);

void fx_framebuffer_bind(struct fx_framebuffer *buffer);

void fx_framebuffer_bind_wlr_fbo(struct fx_renderer *renderer);

void fx_framebuffer_update(struct fx_renderer *fx_renderer, struct fx_framebuffer *fx_buffer,
		int width, int height);

void fx_framebuffer_add_stencil_buffer(struct fx_framebuffer *buffer, int width, int height);

void fx_framebuffer_release(struct fx_framebuffer *buffer);

///
/// fx_texture
///

struct fx_texture *fx_get_texture(struct wlr_texture *wlr_texture);

struct fx_texture *fx_texture_from_buffer(struct fx_renderer *fx_renderer,
	struct wlr_buffer *buffer);

void fx_texture_destroy(struct fx_texture *texture);

bool wlr_texture_is_fx(struct wlr_texture *wlr_texture);

void wlr_gles2_texture_get_fx_attribs(struct fx_texture *texture,
		struct wlr_gles2_texture_attribs *attribs);

///
/// fx_renderer
///

void fx_renderer_init_addon(struct wlr_egl *egl, struct wlr_output *output,
		struct wlr_addon_set *addons, const void * owner);

struct fx_renderer *fx_renderer_addon_find(struct wlr_addon_set *addons,
		const void * owner);

struct fx_renderer *fx_renderer_create(struct wlr_egl *egl, struct wlr_output *output);

void fx_renderer_fini(struct fx_renderer *renderer);

void fx_renderer_begin(struct fx_renderer *renderer, int width, int height);

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

void fx_render_blur(struct fx_renderer *renderer, const float matrix[static 9],
		struct wlr_gles2_texture_attribs *texture, struct blur_shader *shader,
		const struct wlr_box *box, int blur_radius);

#endif
