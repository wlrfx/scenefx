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

enum fx_rounded_quad_shader_source {
	SHADER_SOURCE_QUAD_ROUND = 1,
	SHADER_SOURCE_QUAD_ROUND_TOP_LEFT = 2,
	SHADER_SOURCE_QUAD_ROUND_TOP_RIGHT = 3,
	SHADER_SOURCE_QUAD_ROUND_BOTTOM_RIGHT = 4,
	SHADER_SOURCE_QUAD_ROUND_BOTTOM_LEFT = 5,
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

	GLint window_half_size;
	GLint window_position;
	GLint window_radius;
};

bool link_quad_round_program(struct quad_round_shader *shader, enum fx_rounded_quad_shader_source source);

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

	int max_len;
};

bool link_quad_grad_round_program(struct quad_grad_round_shader *shader, enum fx_rounded_quad_shader_source source, int max_len);

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

struct rounded_border_corner_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint is_top_left;
	GLint is_top_right;
	GLint is_bottom_left;
	GLint is_bottom_right;
	GLint position;
	GLint radius;
	GLint half_size;
	GLint half_thickness;
};

bool link_rounded_border_corner_program(struct rounded_border_corner_shader *shader);

struct rounded_grad_border_corner_shader {
	int max_len;
	GLuint program;
	GLint proj;
	GLint is_top_left;
	GLint is_top_right;
	GLint is_bottom_left;
	GLint is_bottom_right;
	GLint position;
	GLint radius;
	GLint half_size;
	GLint half_thickness;

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

bool link_rounded_grad_border_corner_program(struct rounded_grad_border_corner_shader *shader, int max_len);

struct box_shadow_shader {
	GLuint program;
	GLint proj;
	GLint color;
	GLint pos_attrib;
	GLint position;
	GLint size;
	GLint blur_sigma;
	GLint corner_radius;
	GLint window_position;
	GLint window_half_size;
	GLint window_corner_radius;
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
