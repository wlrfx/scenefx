#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <scenefx/types/fx/clipped_region.h>

#include "render/fx_renderer/shaders.h"

#include "scenefx/types/fx/gradient.h"
#include "scenefx/types/wlr_scene.h"

// shaders
#include "GLES2/gl2.h"
#include "common_vert_src.h"
#include "gradient_frag_src.h"
#include "corner_alpha_frag_src.h"
#include "quad_frag_src.h"
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

void uniform_corner_radii_set(const struct shader_corner_radii *uniform,
		const struct fx_corner_fradii *corners) {
	glUniform1f(uniform->top_left, corners->top_left);
	glUniform1f(uniform->top_right, corners->top_right);
	glUniform1f(uniform->bottom_left, corners->bottom_left);
	glUniform1f(uniform->bottom_right, corners->bottom_right);
}

// Shaders

struct quad_shader_defines {
	int32_t max_gradient_colors;
};

static GLchar* print_quad_shader_defines(struct quad_shader_defines const defines) {
	GLchar* const buffer = malloc(2048 * sizeof(GLchar));
	snprintf(buffer, 2048,
			"#define SHADER_PREAMBLE 1\n"
			"#define FILL_SOLID_COLOR %d\n#define FILL_GRADIENT %d\n"
			"#define GRADIENT_LINEAR %d\n#define GRADIENT_RADIAL %d\n"
			"#define GRADIENT_CONIC %d\n#define MAX_GRADIENT_COLORS %d\n"
			"#define ERROR_COLOR vec4(252.0 / 255.0,15.0 / 255.0, 192.0 / 255.0, 1.0)",
			FILL_SOLID_COLOR, FILL_GRADIENT, GRADIENT_LINEAR, GRADIENT_RADIAL,
			GRADIENT_CONIC, defines.max_gradient_colors);
	return buffer;
}

bool link_quad_program(struct quad_shader *shader, int32_t max_gradient_colors) {
	GLchar* shader_defines = print_quad_shader_defines((struct quad_shader_defines){
			.max_gradient_colors = max_gradient_colors,
		});	
	// TODO: Automatic adjustment of buffer size. Effectively string
	//       concatenation.
	GLchar quad_src[4 * 4096];
	snprintf(quad_src, sizeof(quad_src),
		"%s\n%s\n%s\n%s", shader_defines, quad_frag_src, gradient_frag_src, corner_alpha_frag_src);
	free(shader_defines);
	printf("%s\n", quad_src);

	GLuint prog;
	shader->program = prog = link_program(quad_src);
	if (!shader->program) {
		return false;
	}

	shader->gradient_max_colors = max_gradient_colors;

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");

	shader->effects.clip.enabled = glGetUniformLocation(prog, "effects_clip");
	shader->effects.clip.size = glGetUniformLocation(prog, "clip_size");
	shader->effects.clip.position = glGetUniformLocation(prog, "clip_position");
	shader->effects.clip.radius.top_left = glGetUniformLocation(prog, "clip_radius_top_left");
	shader->effects.clip.radius.top_right = glGetUniformLocation(prog, "clip_radius_top_right");
	shader->effects.clip.radius.bottom_left = glGetUniformLocation(prog, "clip_radius_bottom_left");
	shader->effects.clip.radius.bottom_right = glGetUniformLocation(prog, "clip_radius_bottom_right");

	shader->effects.rounding.enabled = glGetUniformLocation(prog, "effects_rounding");
	shader->effects.rounding.radius.top_left = glGetUniformLocation(prog, "radius_top_left");
	shader->effects.rounding.radius.top_right = glGetUniformLocation(prog, "radius_top_right");
	shader->effects.rounding.radius.bottom_left = glGetUniformLocation(prog, "radius_bottom_left");
	shader->effects.rounding.radius.bottom_right = glGetUniformLocation(prog, "radius_bottom_right");
	shader->effects.rounding.power = glGetUniformLocation(prog, "rounding_power");

	shader->fill_type = glGetUniformLocation(prog, "fill_type");

	shader->gradient_kind = glGetUniformLocation(prog, "gradient_kind");
	shader->gradient_colors = glGetUniformLocation(prog, "gradient_colors");
	shader->gradient_colors_size = glGetUniformLocation(prog, "gradient_colors_size");
	shader->gradient_size = glGetUniformLocation(prog, "gradient_size");
	shader->gradient_angle = glGetUniformLocation(prog, "gradient_angle");
	shader->gradient_blend = glGetUniformLocation(prog, "gradient_blend");
	shader->gradient_box = glGetUniformLocation(prog, "gradient_box");
	shader->gradient_origin = glGetUniformLocation(prog, "gradient_origin");

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
