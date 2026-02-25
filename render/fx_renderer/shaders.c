#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <scenefx/types/fx/clipped_region.h>

#include "render/fx_renderer/shaders.h"

// shaders
#include "GLES2/gl2.h"
#include "common_vert_src.h"
#include "gradient_frag_src.h"
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
	GLuint vert = compile_shader(GL_VERTEX_SHADER, common_vert_src);
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

void uniform_corner_radii_set(struct shader_corner_radii *uniform, struct fx_corner_fradii *corners) {
	glUniform1f(uniform->top_left, corners->top_left);
	glUniform1f(uniform->top_right, corners->top_right);
	glUniform1f(uniform->bottom_left, corners->bottom_left);
	glUniform1f(uniform->bottom_right, corners->bottom_right);
}
// Shaders

bool link_quad_program(struct quad_shader *shader) {
	GLchar quad_src[4096];
	snprintf(quad_src, sizeof(quad_src), "%s\n%s", quad_frag_src,
		corner_alpha_frag_src);

	GLuint prog;
	shader->program = prog = link_program(quad_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->clip_size = glGetUniformLocation(prog, "clip_size");
	shader->clip_position = glGetUniformLocation(prog, "clip_position");
	shader->clip_radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clip_radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clip_radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clip_radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_quad_grad_program(struct quad_grad_shader *shader, int max_len) {
	GLchar quad_src_part[2048];
	GLchar quad_src[4096];
	snprintf(quad_src_part, sizeof(quad_src_part),
		quad_grad_frag_src, max_len);
	snprintf(quad_src, sizeof(quad_src),
		"%s\n%s", quad_src_part, gradient_frag_src);

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
	snprintf(quad_src, sizeof(quad_src), "%s\n%s", quad_round_frag_src,
		corner_alpha_frag_src);

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
	shader->radius.top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->radius.top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->radius.bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->radius.bottom_right = glGetUniformLocation(prog, "radius_bottom_right");

	shader->clip_size = glGetUniformLocation(prog, "clip_size");
	shader->clip_position = glGetUniformLocation(prog, "clip_position");
	shader->clip_radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clip_radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clip_radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clip_radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_quad_grad_round_program(struct quad_grad_round_shader *shader, int max_len) {
	GLchar quad_src_part[2048];
	GLchar quad_src[8192];
	snprintf(quad_src_part, sizeof(quad_src_part),
		quad_grad_round_frag_src, max_len);
	snprintf(quad_src, sizeof(quad_src),
		"%s\n%s\n%s", quad_src_part, gradient_frag_src, corner_alpha_frag_src);

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
	shader->radius.top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->radius.top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->radius.bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->radius.bottom_right = glGetUniformLocation(prog, "radius_bottom_right");

	shader->grad_size = glGetUniformLocation(prog, "grad_size");
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

bool link_tex_program(struct tex_shader *shader, enum fx_tex_shader_source source,
		bool effects) {
	GLchar frag_src_part[4096];
	GLchar frag_src[8192];
	snprintf(frag_src_part, sizeof(frag_src_part),
		tex_frag_src, source, effects);
	snprintf(frag_src, sizeof(frag_src),
		"%s\n%s\n", frag_src_part, effects ? corner_alpha_frag_src : "");

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

	shader->discard_transparent = glGetUniformLocation(prog, "discard_transparent");

	if (!effects) {
		return true;
	}
	shader->effects.size = glGetUniformLocation(prog, "size");
	shader->effects.position = glGetUniformLocation(prog, "position");
	shader->effects.radius.top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->effects.radius.top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->effects.radius.bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->effects.radius.bottom_right = glGetUniformLocation(prog, "radius_bottom_right");

	shader->effects.clip_size = glGetUniformLocation(prog, "clip_size");
	shader->effects.clip_position = glGetUniformLocation(prog, "clip_position");
	shader->effects.clip_radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->effects.clip_radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->effects.clip_radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->effects.clip_radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_box_shadow_program(struct box_shadow_shader *shader) {
	GLchar shadow_src[8192];
	snprintf(shadow_src, sizeof(shadow_src), "%s\n%s", box_shadow_frag_src,
		corner_alpha_frag_src);

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
	shader->clip_position = glGetUniformLocation(prog, "clip_position");
	shader->clip_size = glGetUniformLocation(prog, "clip_size");
	shader->clip_radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clip_radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clip_radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clip_radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

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
