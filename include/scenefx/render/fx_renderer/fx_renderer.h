#ifndef SCENEFX_FX_OPENGL_H
#define SCENEFX_FX_OPENGL_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wlr/backend.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_buffer.h>

#include "render/fx_renderer/shaders.h"

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

///
/// fx_renderer
///

// TODO: make this private
struct fx_renderer {
	struct wlr_renderer wlr_renderer;

	float projection[9];
	struct wlr_egl *egl;
	int drm_fd;

	const char *exts_str;
	struct {
		bool EXT_read_format_bgra;
		bool KHR_debug;
		bool OES_egl_image_external;
		bool OES_egl_image;
		bool EXT_texture_type_2_10_10_10_REV;
		bool OES_texture_half_float_linear;
		bool EXT_texture_norm16;
		bool EXT_disjoint_timer_query;
	} exts;

	struct {
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
		PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
		PFNGLDEBUGMESSAGECONTROLKHRPROC glDebugMessageControlKHR;
		PFNGLPOPDEBUGGROUPKHRPROC glPopDebugGroupKHR;
		PFNGLPUSHDEBUGGROUPKHRPROC glPushDebugGroupKHR;
		PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES;
		PFNGLGETGRAPHICSRESETSTATUSKHRPROC glGetGraphicsResetStatusKHR;
		PFNGLGENQUERIESEXTPROC glGenQueriesEXT;
		PFNGLDELETEQUERIESEXTPROC glDeleteQueriesEXT;
		PFNGLQUERYCOUNTEREXTPROC glQueryCounterEXT;
		PFNGLGETQUERYOBJECTIVEXTPROC glGetQueryObjectivEXT;
		PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT;
		PFNGLGETINTEGER64VEXTPROC glGetInteger64vEXT;
	} procs;

	struct {
		struct quad_shader quad;
		struct quad_round_shader quad_round;
		struct quad_round_shader quad_round_tl;
		struct quad_round_shader quad_round_tr;
		struct quad_round_shader quad_round_bl;
		struct quad_round_shader quad_round_br;
		struct tex_shader tex_rgba;
		struct tex_shader tex_rgbx;
		struct tex_shader tex_ext;
		struct box_shadow_shader box_shadow;
		struct rounded_border_corner_shader rounded_border_corner;
		struct stencil_mask_shader stencil_mask;
		struct blur_shader blur1;
		struct blur_shader blur2;
		struct blur_effects_shader blur_effects;
	} shaders;

	struct wl_list buffers; // fx_framebuffer.link
	struct wl_list textures; // fx_texture.link

	struct fx_framebuffer *current_buffer;
	uint32_t viewport_width, viewport_height;

	// Set to true when 'wlr_renderer_begin_buffer_pass' is called instead of
	// our custom 'fx_renderer_begin_buffer_pass' function
	bool basic_renderer;

	// The region where there's blur
	pixman_region32_t blur_padding_region;
};


struct wlr_texture *fx_texture_from_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *buffer);

void fx_texture_get_attribs(struct wlr_texture *texture,
	struct fx_texture_attribs *attribs);

#endif
