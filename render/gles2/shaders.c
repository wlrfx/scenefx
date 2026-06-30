#include <EGL/egl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include <scenefx/types/fx/clipped_region.h>

#include "render/gles2/shaders.h"

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
		GLint error_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &error_length);

		char *error_msg = calloc(error_length, sizeof(*error_msg));
		glGetShaderInfoLog(shader, error_length, &error_length, error_msg);
		wlr_log(WLR_ERROR, "Failed to compile shader: %s", error_msg);
		free(error_msg);

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
		GLint error_length = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &error_length);

		char *error_msg = calloc(error_length, sizeof(*error_msg));
		glGetProgramInfoLog(prog, error_length, &error_length, error_msg);
		wlr_log(WLR_ERROR, "Failed to link shader: %s", error_msg);
		free(error_msg);

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

bool check_egl_ext(const char *exts, const char *ext) {
	size_t extlen = strlen(ext);
	const char *end = exts + strlen(exts);

	while (exts < end) {
		if (*exts == ' ') {
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

void load_egl_proc(void *proc_ptr, const char *name) {
	void *proc = (void *)eglGetProcAddress(name);
	if (proc == NULL) {
		wlr_log(WLR_ERROR, "eglGetProcAddress(%s) failed", name);
		abort();
	}
	*(void **)proc_ptr = proc;
}

// Shaders

bool link_quad_program(struct quad_shader *shader, bool clip) {
	GLchar quad_src_part[2048];
	GLchar quad_src[4096];
	snprintf(quad_src_part, sizeof(quad_src_part),
		quad_frag_src, clip);
	snprintf(quad_src, sizeof(quad_src),
		"%s\n%s\n", quad_src_part, clip ? corner_alpha_frag_src : "");

	GLuint prog;
	shader->program = prog = link_program(quad_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");

	if (!clip) {
		return true;
	}
	shader->effects.clip_size = glGetUniformLocation(prog, "clip_size");
	shader->effects.clip_position = glGetUniformLocation(prog, "clip_position");
	shader->effects.clip_radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->effects.clip_radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->effects.clip_radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->effects.clip_radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

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

static bool link_tex_program(struct tex_shader_variant* shader,
		enum fx_tex_shader_source source, enum fx_tex_shader_effects effects) {
	const bool needs_corner_rounding = effects & SHADER_TEXTURE_EFFECT_ROUND_CORNERS
		|| effects & SHADER_TEXTURE_EFFECT_CLIPPING;

	GLchar frag_src_part[4096];
	GLchar frag_src[8192];
	snprintf(frag_src_part, sizeof(frag_src_part),
		tex_frag_src, source, effects);
	snprintf(frag_src, sizeof(frag_src),
		"%s\n%s\n", frag_src_part, needs_corner_rounding ? corner_alpha_frag_src : "");

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

	shader->corner_rounding.size = glGetUniformLocation(prog, "size");
	shader->corner_rounding.position = glGetUniformLocation(prog, "position");
	shader->corner_rounding.radius.top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->corner_rounding.radius.top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->corner_rounding.radius.bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->corner_rounding.radius.bottom_right = glGetUniformLocation(prog, "radius_bottom_right");

	shader->clipping.size = glGetUniformLocation(prog, "clip_size");
	shader->clipping.position = glGetUniformLocation(prog, "clip_position");
	shader->clipping.radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->clipping.radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->clipping.radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->clipping.radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	return true;
}

bool link_tex_programs(struct tex_shader *shader, enum fx_tex_shader_source source) {
	memset(&shader->variants, 0, sizeof(*shader->variants));
	for (enum fx_tex_shader_effects effects = 0;
			effects < SHADER_TEXTURE_EFFECT_LAST;
			effects++) {
		if (!link_tex_program(&shader->variants[effects], source, effects)) {
			wlr_log(WLR_ERROR,
					"Could not link tex shader: Source \"%d\", Effects: \"%d\"",
					source, effects);
			return false;
		}
	}
	return true;
}

void delete_tex_programs(struct tex_shader *shader) {
	for (enum fx_tex_shader_effects effects = 0;
			effects < SHADER_TEXTURE_EFFECT_LAST;
			effects++) {
		struct tex_shader_variant variant = shader->variants[effects];
		if (variant.program > 0) {
			glDeleteProgram(variant.program);
		}
	}
}

struct tex_shader_variant *get_tex_program(struct tex_shader *shader,
		enum fx_tex_shader_effects effects) {
	assert(effects < SHADER_TEXTURE_EFFECT_LAST);
	return &shader->variants[effects];
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
