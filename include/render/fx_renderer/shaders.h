#ifndef _FX_SHADERS_H
#define _FX_SHADERS_H

#include <GLES2/gl2.h>
#include <stdbool.h>

struct fx_renderer;

GLuint compile_shader(GLuint type, const GLchar *src);

GLuint link_program(const GLchar *frag_src);

bool check_gl_ext(const char *exts, const char *ext);

void load_gl_proc(void *proc_ptr, const char *name);

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

bool link_quad_program(struct quad_shader *shader);

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

bool link_quad_grad_program(struct quad_grad_shader *shader, int max_len);

struct quad_round_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint size;
	GLint position;
	GLint radius;

	GLint clip_size;
	GLint clip_position;
	GLint clip_corner_radius;
	GLint clip_round_top_left;
	GLint clip_round_top_right;
	GLint clip_round_bottom_left;
	GLint clip_round_bottom_right;

	GLint round_top_left;
	GLint round_top_right;
	GLint round_bottom_left;
	GLint round_bottom_right;
};

bool link_quad_round_program(struct quad_round_shader *shader);

struct quad_grad_round_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint size;
	GLint position;
	GLint radius;

	GLint colors;
	GLint grad_size;
	GLint degree;
	GLint grad_box;
	GLint linear;
	GLint origin;
	GLint count;
	GLint blend;

	GLint round_top_left;
	GLint round_top_right;
	GLint round_bottom_left;
	GLint round_bottom_right;

	int max_len;
};

bool link_quad_grad_round_program(struct quad_grad_round_shader *shader, int max_len);

struct tex_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint alpha;
	GLint pos_attrib;
	GLint size;
	GLint position;
	GLint radius;
	GLint discard_transparent;
	GLint dim;
	GLint dim_color;

	GLint round_top_left;
	GLint round_top_right;
	GLint round_bottom_left;
	GLint round_bottom_right;
};

bool link_tex_program(struct tex_shader *shader, enum fx_tex_shader_source source);

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
	GLint clip_corner_radius;
	GLint clip_round_top_left;
	GLint clip_round_top_right;
	GLint clip_round_bottom_left;
	GLint clip_round_bottom_right;
};

bool link_box_shadow_program(struct box_shadow_shader *shader);

struct blur_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint pos_attrib;
	GLint radius;
	GLint halfpixel;
};

bool link_blur1_program(struct blur_shader *shader);
bool link_blur2_program(struct blur_shader *shader);

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

bool link_blur_effects_program(struct blur_effects_shader *shader);

#endif
