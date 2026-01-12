#ifndef _FX_SHADERS_H
#define _FX_SHADERS_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <scenefx/types/fx/clipped_region.h>
#include "types/fx/clipped_region.h"

struct fx_renderer;

GLuint compile_shader(GLuint type, const GLchar *src);

GLuint link_program(const GLchar *frag_src, GLint client_version);

bool check_gl_ext(const char *exts, const char *ext);

void load_gl_proc(void *proc_ptr, const char *name);

enum fx_tex_shader_source {
	SHADER_SOURCE_TEXTURE_RGBA = 1,
	SHADER_SOURCE_TEXTURE_RGBX = 2,
	SHADER_SOURCE_TEXTURE_EXTERNAL = 3,
};

struct shader_corner_radii {
	GLint top_left;
	GLint top_right;
	GLint bottom_left;
	GLint bottom_right;
};

void uniform_corner_radii_set(struct shader_corner_radii *uniform, struct fx_corner_fradii *corners);

struct quad_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;

	GLint clip_size;
	GLint clip_position;
	struct shader_corner_radii clip_radius;
};

bool link_quad_program(struct quad_shader *shader, GLint client_version);

struct quad_grad_shader {
	int max_len;

	GLuint program;
	GLint proj;
	GLint colors;
	GLint size;
	GLint degree;
	GLint grad_box;
	GLint pos_attrib;
	GLint linear;
	GLint origin;
	GLint count;
	GLint blend;
};

bool link_quad_grad_program(struct quad_grad_shader *shader, GLint client_version, int max_len);

struct quad_round_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint size;
	GLint position;

	struct shader_corner_radii radius;

	GLint clip_size;
	GLint clip_position;
	struct shader_corner_radii clip_radius;
};

bool link_quad_round_program(struct quad_round_shader *shader, GLint client_version);

struct quad_grad_round_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint size;
	GLint position;

	GLint colors;
	GLint grad_size;
	GLint degree;
	GLint grad_box;
	GLint linear;
	GLint origin;
	GLint count;
	GLint blend;

	struct shader_corner_radii radius;

	int max_len;
};

bool link_quad_grad_round_program(struct quad_grad_round_shader *shader, GLint client_version, int max_len);

struct tex_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint alpha;
	GLint pos_attrib;
	GLint size;
	GLint position;
	struct shader_corner_radii radius;

	GLint discard_transparent;

	GLint clip_size;
	GLint clip_position;
	struct shader_corner_radii clip_radius;
};

bool link_tex_program(struct tex_shader *shader, GLint client_version, enum fx_tex_shader_source source);

struct box_shadow_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint position;
	GLint size;
	GLint blur_sigma;
	GLint corner_radius;

	GLint clip_position;
	GLint clip_size;
	struct shader_corner_radii clip_radius;
};

bool link_box_shadow_program(struct box_shadow_shader *shader, GLint client_version);

struct blur_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint pos_attrib;
	GLint radius;
	GLint halfpixel;
};

bool link_blur1_program(struct blur_shader *shader, GLint client_version);
bool link_blur2_program(struct blur_shader *shader, GLint client_version);

struct blur_effects_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint pos_attrib;
	GLfloat noise;
	GLfloat brightness;
	GLfloat contrast;
	GLfloat saturation;
};

bool link_blur_effects_program(struct blur_effects_shader *shader, GLint client_version);

struct drop_shadow_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint blur_sigma;
	GLint direction;
	GLint pos_attrib;
	GLint size;
};

bool link_drop_shadow_program(struct drop_shadow_shader *shader, GLint client_version);

struct drop_shadow_final_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint color;
	GLint pos_attrib;
};

bool link_drop_shadow_final_program(struct drop_shadow_final_shader *shader, GLint client_version);

#endif
