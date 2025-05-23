#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "render/fx_renderer/shaders.h"

// shaders
#include "GLES2/gl2.h"
// gles3
#include "common_vert_gles3_src.h"
#include "gradient_frag_gles3_src.h"
#include "corner_alpha_frag_gles3_src.h"
#include "quad_frag_gles3_src.h"
#include "quad_grad_frag_gles3_src.h"
#include "quad_round_frag_gles3_src.h"
#include "quad_grad_round_frag_gles3_src.h"
#include "tex_frag_gles3_src.h"
#include "box_shadow_frag_gles3_src.h"
#include "blur1_frag_gles3_src.h"
#include "blur2_frag_gles3_src.h"
#include "blur_effects_frag_gles3_src.h"
// gles2
#include "common_vert_gles2_src.h"
#include "gradient_frag_gles2_src.h"
#include "corner_alpha_frag_gles2_src.h"
#include "quad_frag_gles2_src.h"
#include "quad_grad_frag_gles2_src.h"
#include "quad_round_frag_gles2_src.h"
#include "quad_grad_round_frag_gles2_src.h"
#include "tex_frag_gles2_src.h"
#include "box_shadow_frag_gles2_src.h"
#include "blur1_frag_gles2_src.h"
#include "blur2_frag_gles2_src.h"
#include "blur_effects_frag_gles2_src.h"

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

GLuint link_program(const GLchar *frag_src, GLint client_version) {
	const GLchar *vert_src = client_version > 2 ? common_vert_gles3_src : common_vert_gles2_src;
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

bool link_quad_program(struct quad_shader *shader, GLint client_version) {
	GLchar quad_src[4096];
	if (client_version > 2) {
		snprintf(quad_src, sizeof(quad_src), "%s\n%s", quad_frag_gles3_src,
			corner_alpha_frag_gles3_src);
	} else {
		snprintf(quad_src, sizeof(quad_src), "%s\n%s", quad_frag_gles2_src,
			corner_alpha_frag_gles2_src);
	}

	GLuint prog;
	shader->program = prog = link_program(quad_src, client_version);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->clip_size = glGetUniformLocation(prog, "clip_size");
	shader->clip_position = glGetUniformLocation(prog, "clip_position");
	shader->clip_radius_top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clip_radius_top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clip_radius_bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clip_radius_bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_quad_grad_program(struct quad_grad_shader *shader, GLint client_version, int max_len) {
	GLchar quad_src_part[2048];
	GLchar quad_src[4096];
	if (client_version > 2) {
		snprintf(quad_src_part, sizeof(quad_src_part),
			quad_grad_frag_gles3_src, max_len);
		snprintf(quad_src, sizeof(quad_src),
			"%s\n%s", quad_src_part, gradient_frag_gles3_src);
	} else {
		snprintf(quad_src_part, sizeof(quad_src_part),
			quad_grad_frag_gles2_src, max_len);
		snprintf(quad_src, sizeof(quad_src),
			"%s\n%s", quad_src_part, gradient_frag_gles2_src);
	}

	GLuint prog;
	shader->program = prog = link_program(quad_src, client_version);
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

bool link_quad_round_program(struct quad_round_shader *shader, GLint client_version) {
	GLchar quad_src[4096];
	if (client_version > 2) {
		snprintf(quad_src, sizeof(quad_src), "%s\n%s", quad_round_frag_gles3_src,
			corner_alpha_frag_gles3_src);
	} else {
		snprintf(quad_src, sizeof(quad_src), "%s\n%s", quad_round_frag_gles2_src,
			corner_alpha_frag_gles2_src);
	}

	GLuint prog;
	shader->program = prog = link_program(quad_src, client_version);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");
	shader->radius_top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->radius_top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->radius_bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->radius_bottom_right = glGetUniformLocation(prog, "radius_bottom_right");

	shader->clip_size = glGetUniformLocation(prog, "clip_size");
	shader->clip_position = glGetUniformLocation(prog, "clip_position");
	shader->clip_radius_top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clip_radius_top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clip_radius_bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clip_radius_bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_quad_grad_round_program(struct quad_grad_round_shader *shader, GLint client_version, int max_len) {
	GLchar quad_src_part[2048];
	GLchar quad_src[4096];
	if (client_version > 2) {
		snprintf(quad_src_part, sizeof(quad_src_part),
			quad_grad_round_frag_gles3_src, max_len);
		snprintf(quad_src, sizeof(quad_src),
			"%s\n%s\n%s", quad_src_part, gradient_frag_gles3_src, corner_alpha_frag_gles3_src);
	} else {
		snprintf(quad_src_part, sizeof(quad_src_part),
			quad_grad_round_frag_gles2_src, max_len);
		snprintf(quad_src, sizeof(quad_src),
			"%s\n%s\n%s", quad_src_part, gradient_frag_gles2_src, corner_alpha_frag_gles2_src);
	}

	GLuint prog;
	shader->program = prog = link_program(quad_src, client_version);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");
	shader->radius_top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->radius_top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->radius_bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->radius_bottom_right = glGetUniformLocation(prog, "radius_bottom_right");

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

bool link_tex_program(struct tex_shader *shader, GLint client_version, enum fx_tex_shader_source source) {
	GLchar frag_src_part[2048];
	GLchar frag_src[4096];
	if (client_version > 2) {
		snprintf(frag_src_part, sizeof(frag_src_part),
			tex_frag_gles3_src, source);
		snprintf(frag_src, sizeof(frag_src),
			"%s\n%s\n", frag_src_part, corner_alpha_frag_gles3_src);
	} else {
		snprintf(frag_src_part, sizeof(frag_src_part),
			tex_frag_gles2_src, source);
		snprintf(frag_src, sizeof(frag_src),
			"%s\n%s\n", frag_src_part, corner_alpha_frag_gles2_src);
	}

	GLuint prog;
	shader->program = prog = link_program(frag_src, client_version);
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
	shader->radius_top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->radius_top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->radius_bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->radius_bottom_right = glGetUniformLocation(prog, "radius_bottom_right");
	shader->discard_transparent = glGetUniformLocation(prog, "discard_transparent");

	return true;
}

bool link_box_shadow_program(struct box_shadow_shader *shader, GLint client_version) {
	GLchar shadow_src[8192];
	if (client_version > 2) {
		snprintf(shadow_src, sizeof(shadow_src), "%s\n%s", box_shadow_frag_gles3_src,
			corner_alpha_frag_gles3_src);
	} else {
		snprintf(shadow_src, sizeof(shadow_src), "%s\n%s", box_shadow_frag_gles2_src,
			corner_alpha_frag_gles2_src);
	}

	GLuint prog;
	shader->program = prog = link_program(shadow_src, client_version);
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
	shader->clip_radius_top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clip_radius_top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clip_radius_bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clip_radius_bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_blur1_program(struct blur_shader *shader, GLint client_version) {
	GLuint prog;
	shader->program = prog = client_version > 2 ? link_program(blur1_frag_gles3_src, client_version)
		: link_program(blur1_frag_gles2_src, client_version);
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

bool link_blur2_program(struct blur_shader *shader, GLint client_version) {
	GLuint prog;
	shader->program = prog = client_version > 2 ? link_program(blur2_frag_gles3_src, client_version)
		: link_program(blur2_frag_gles2_src, client_version);
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

bool link_blur_effects_program(struct blur_effects_shader *shader, GLint client_version) {
	GLuint prog;
	shader->program = prog = client_version > 2 ? link_program(blur_effects_frag_gles3_src, client_version)
		: link_program(blur_effects_frag_gles2_src, client_version);
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
