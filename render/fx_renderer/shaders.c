#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "render/fx_renderer/shaders.h"

// shaders
#include "GLES2/gl2.h"
#include "common_vert_src.h"
#include "gradient_frag_src.h"
#include "round_rect_sdf_frag_src.h"
#include "corner_alpha_frag_src.h"
#include "quad_frag_src.h"
#include "quad_grad_frag_src.h"
#include "quad_round_frag_src.h"
#include "quad_grad_round_frag_src.h"
#include "tex_frag_src.h"
#include "box_shadow_frag_src.h"
#include "blur1_frag_src.h"
#include "blur2_frag_src.h"
#include "blur_effects_frag_src.h"

GLuint compile_shader(GLuint type, const GLchar *src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok == GL_FALSE) {
		wlr_log(WLR_ERROR, "Failed to compile shader");
		glDeleteShader(shader);
		shader = 0;
	}

	return shader;
}

GLuint link_program(const GLchar *frag_src) {
	const GLchar *vert_src = common_vert_src;
	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vert) {
		goto error;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!frag) {
		glDeleteShader(vert);
		goto error;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		wlr_log(WLR_ERROR, "Failed to link shader");
		glDeleteProgram(prog);
		goto error;
	}

	return prog;

error:
	return 0;
}


bool check_gl_ext(const char *exts, const char *ext) {
	size_t extlen = strlen(ext);
	const char *end = exts + strlen(exts);

	while (exts < end) {
		if (exts[0] == ' ') {
			exts++;
			continue;
		}
		size_t n = strcspn(exts, " ");
		if (n == extlen && strncmp(ext, exts, n) == 0) {
			return true;
		}
		exts += n;
	}
	return false;
}

void load_gl_proc(void *proc_ptr, const char *name) {
	void *proc = (void *)eglGetProcAddress(name);
	if (proc == NULL) {
		wlr_log(WLR_ERROR, "FX RENDERER: eglGetProcAddress(%s) failed", name);
		abort();
	}
	*(void **)proc_ptr = proc;
}

// Shaders

bool link_quad_program(struct quad_shader *shader) {
	GLuint prog;
	shader->program = prog = link_program(quad_frag_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");

	return true;
}

bool link_quad_grad_program(struct quad_grad_shader *shader, int max_len) {
	GLchar quad_src[2048];
	snprintf(quad_src, sizeof(quad_src),
			"#define LEN %d\n%s\n%s", max_len, quad_grad_frag_src, gradient_frag_src);

	GLuint prog;
	shader->program = prog = link_program(quad_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->size = glGetUniformLocation(prog, "size");
	shader->colors = glGetUniformLocation(prog, "colors");
	shader->degree = glGetUniformLocation(prog, "degree");
	shader->grad_box = glGetUniformLocation(prog, "grad_box");
	shader->linear = glGetUniformLocation(prog, "linear");
	shader->origin = glGetUniformLocation(prog, "origin");
	shader->count = glGetUniformLocation(prog, "count");
	shader->blend = glGetUniformLocation(prog, "blend");

	shader->max_len = max_len;

	return true;
}

bool link_quad_round_program(struct quad_round_shader *shader) {
	GLchar quad_src[4096];
	snprintf(quad_src, sizeof(quad_src), "%s\n%s\n%s", quad_round_frag_src,
		corner_alpha_frag_src, round_rect_sdf_frag_src);

	GLuint prog;
	shader->program = prog = link_program(quad_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");
	shader->radius = glGetUniformLocation(prog, "radius");
	shader->window_half_size = glGetUniformLocation(prog, "window_half_size");
	shader->window_position = glGetUniformLocation(prog, "window_position");
	shader->window_radius = glGetUniformLocation(prog, "window_radius");

	shader->round_top_left = glGetUniformLocation(prog, "round_top_left");
	shader->round_top_right = glGetUniformLocation(prog, "round_top_right");
	shader->round_bottom_left = glGetUniformLocation(prog, "round_bottom_left");
	shader->round_bottom_right = glGetUniformLocation(prog, "round_bottom_right");

	return true;
}

bool link_quad_grad_round_program(struct quad_grad_round_shader *shader, int max_len) {
	GLchar quad_src[4096];
	snprintf(quad_src, sizeof(quad_src), "#define LEN %d\n%s\n%s\n%s\n%s", max_len, quad_grad_round_frag_src,
			 gradient_frag_src, corner_alpha_frag_src, round_rect_sdf_frag_src);

	GLuint prog;
	shader->program = prog = link_program(quad_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");
	shader->radius = glGetUniformLocation(prog, "radius");

	shader->grad_size = glGetUniformLocation(prog, "grad_size");
	shader->colors = glGetUniformLocation(prog, "colors");
	shader->degree = glGetUniformLocation(prog, "degree");
	shader->grad_box = glGetUniformLocation(prog, "grad_box");
	shader->linear = glGetUniformLocation(prog, "linear");
	shader->origin = glGetUniformLocation(prog, "origin");
	shader->count = glGetUniformLocation(prog, "count");
	shader->blend = glGetUniformLocation(prog, "blend");

	shader->round_top_left = glGetUniformLocation(prog, "round_top_left");
	shader->round_top_right = glGetUniformLocation(prog, "round_top_right");
	shader->round_bottom_left = glGetUniformLocation(prog, "round_bottom_left");
	shader->round_bottom_right = glGetUniformLocation(prog, "round_bottom_right");

	shader->max_len = max_len;

	return true;
}

bool link_tex_program(struct tex_shader *shader, enum fx_tex_shader_source source) {
	GLchar frag_src[4096];
	snprintf(frag_src, sizeof(frag_src), "#define SOURCE %d\n%s\n%s\n%s", source,
			tex_frag_src, corner_alpha_frag_src, round_rect_sdf_frag_src);

	GLuint prog;
	shader->program = prog = link_program(frag_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->tex = glGetUniformLocation(prog, "tex");
	shader->alpha = glGetUniformLocation(prog, "alpha");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->tex_proj = glGetUniformLocation(prog, "tex_proj");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");
	shader->radius = glGetUniformLocation(prog, "radius");
	shader->discard_transparent = glGetUniformLocation(prog, "discard_transparent");
	shader->dim = glGetUniformLocation(prog, "dim");
	shader->dim_color = glGetUniformLocation(prog, "dim_color");
	shader->round_top_left = glGetUniformLocation(prog, "round_top_left");
	shader->round_top_right = glGetUniformLocation(prog, "round_top_right");
	shader->round_bottom_left = glGetUniformLocation(prog, "round_bottom_left");
	shader->round_bottom_right = glGetUniformLocation(prog, "round_bottom_right");

	return true;
}

bool link_box_shadow_program(struct box_shadow_shader *shader) {
	GLchar shadow_src[4096];
	snprintf(shadow_src, sizeof(shadow_src), "%s\n%s", box_shadow_frag_src,
			round_rect_sdf_frag_src);

	GLuint prog;
	shader->program = prog = link_program(shadow_src);
	if (!shader->program) {
		return false;
	}
	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->position = glGetUniformLocation(prog, "position");
	shader->size = glGetUniformLocation(prog, "size");
	shader->blur_sigma = glGetUniformLocation(prog, "blur_sigma");
	shader->corner_radius = glGetUniformLocation(prog, "corner_radius");
	shader->window_position = glGetUniformLocation(prog, "window_position");
	shader->window_half_size = glGetUniformLocation(prog, "window_half_size");
	shader->window_corner_radius = glGetUniformLocation(prog, "window_corner_radius");

	return true;
}

bool link_blur1_program(struct blur_shader *shader) {
	GLuint prog;
	shader->program = prog = link_program(blur1_frag_src);
	if (!shader->program) {
		return false;
	}
	shader->proj = glGetUniformLocation(prog, "proj");
	shader->tex = glGetUniformLocation(prog, "tex");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->tex_proj = glGetUniformLocation(prog, "tex_proj");
	shader->radius = glGetUniformLocation(prog, "radius");
	shader->halfpixel = glGetUniformLocation(prog, "halfpixel");

	return true;
}

bool link_blur2_program(struct blur_shader *shader) {
	GLuint prog;
	shader->program = prog = link_program(blur2_frag_src);
	if (!shader->program) {
		return false;
	}
	shader->proj = glGetUniformLocation(prog, "proj");
	shader->tex = glGetUniformLocation(prog, "tex");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->tex_proj = glGetUniformLocation(prog, "tex_proj");
	shader->radius = glGetUniformLocation(prog, "radius");
	shader->halfpixel = glGetUniformLocation(prog, "halfpixel");

	return true;
}

bool link_blur_effects_program(struct blur_effects_shader *shader) {
	GLuint prog;
	shader->program = prog = link_program(blur_effects_frag_src);
	if (!shader->program) {
		return false;
	}
	shader->proj = glGetUniformLocation(prog, "proj");
	shader->tex = glGetUniformLocation(prog, "tex");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->tex_proj = glGetUniformLocation(prog, "tex_proj");
	shader->noise = glGetUniformLocation(prog, "noise");
	shader->brightness = glGetUniformLocation(prog, "brightness");
	shader->contrast = glGetUniformLocation(prog, "contrast");
	shader->saturation = glGetUniformLocation(prog, "saturation");

	return true;
}
