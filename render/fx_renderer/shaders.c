#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/shaders.h"

// shaders
#include "common_vert_src.h"
#include "quad_frag_src.h"
#include "tex_frag_src.h"
#include "stencil_mask_frag_src.h"
#include "box_shadow_frag_src.h"
#include "blur1_frag_src.h"
#include "blur2_frag_src.h"

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

static bool link_quad_program(struct quad_shader *shader) {
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

static bool link_tex_program(struct tex_shader *shader,
		enum fx_tex_shader_source source) {
	GLchar frag_src[2048];
	snprintf(frag_src, sizeof(frag_src),
		"#define SOURCE %d\n%s", source, tex_frag_src);

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

	return true;
}

static bool link_stencil_mask_program(struct stencil_mask_shader *shader) {
	GLuint prog;
	shader->program = prog = link_program(stencil_mask_frag_src);
	if (!shader->program) {
		return false;
	}

	shader->proj = glGetUniformLocation(prog, "proj");
	shader->color = glGetUniformLocation(prog, "color");
	shader->pos_attrib = glGetAttribLocation(prog, "pos");
	shader->position = glGetUniformLocation(prog, "position");
	shader->half_size = glGetUniformLocation(prog, "half_size");
	shader->radius = glGetUniformLocation(prog, "radius");

	return true;
}

static bool link_box_shadow_program(struct box_shadow_shader *shader) {
	GLuint prog;
	shader->program = prog = link_program(box_shadow_frag_src);
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

	return true;
}

static bool link_blur_program(struct blur_shader *shader, const char *shader_program) {
	GLuint prog;
	shader->program = prog = link_program(shader_program);
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

bool link_shaders(struct fx_renderer *renderer) {
	// quad fragment shader
	if (!link_quad_program(&renderer->shaders.quad)) {
		wlr_log(WLR_ERROR, "Could not link quad shader");
		goto error;
	}
	// fragment shaders
	if (!link_tex_program(&renderer->shaders.tex_rgba, SHADER_SOURCE_TEXTURE_RGBA)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBA shader");
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_rgbx, SHADER_SOURCE_TEXTURE_RGBX)) {
		wlr_log(WLR_ERROR, "Could not link tex_RGBX shader");
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_ext, SHADER_SOURCE_TEXTURE_EXTERNAL)) {
		wlr_log(WLR_ERROR, "Could not link tex_EXTERNAL shader");
		goto error;
	}

	// stencil mask shader
	if (!link_stencil_mask_program(&renderer->shaders.stencil_mask)) {
		wlr_log(WLR_ERROR, "Could not link stencil mask shader");
		goto error;
	}
	// box shadow shader
	if (!link_box_shadow_program(&renderer->shaders.box_shadow)) {
		wlr_log(WLR_ERROR, "Could not link box shadow shader");
		goto error;
	}

	// Blur shaders
	if (!link_blur_program(&renderer->shaders.blur1, blur1_frag_src)) {
		wlr_log(WLR_ERROR, "Could not link blur1 shader");
		goto error;
	}
	if (!link_blur_program(&renderer->shaders.blur2, blur2_frag_src)) {
		wlr_log(WLR_ERROR, "Could not link blur2 shader");
		goto error;
	}

	return true;

error:
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);
	glDeleteProgram(renderer->shaders.stencil_mask.program);
	glDeleteProgram(renderer->shaders.box_shadow.program);
	glDeleteProgram(renderer->shaders.blur1.program);
	glDeleteProgram(renderer->shaders.blur2.program);

	return false;
}
