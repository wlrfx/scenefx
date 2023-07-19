/*
	The original wlr_renderer was heavily referenced in making this project
	https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/render/gles2
*/

#include <assert.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "render/fx_renderer/fx_renderer.h"
#include "render/fx_renderer/matrix.h"

// shaders
#include "common_vert_src.h"
#include "quad_frag_src.h"
#include "tex_frag_src.h"

static const GLfloat verts[] = {
	1, 0, // top right
	0, 0, // top left
	1, 1, // bottom right
	0, 1, // bottom left
};

static GLuint compile_shader(GLuint type, const GLchar *src) {
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

static GLuint link_program(const GLchar *frag_src) {
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
	shader->tex_attrib = glGetAttribLocation(prog, "texcoord");
	shader->size = glGetUniformLocation(prog, "size");
	shader->position = glGetUniformLocation(prog, "position");
	shader->radius = glGetUniformLocation(prog, "radius");

	return true;
}

static bool check_gl_ext(const char *exts, const char *ext) {
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

static void load_gl_proc(void *proc_ptr, const char *name) {
	void *proc = (void *)eglGetProcAddress(name);
	if (proc == NULL) {
		wlr_log(WLR_ERROR, "GLES2 RENDERER: eglGetProcAddress(%s) failed", name);
		abort();
	}
	*(void **)proc_ptr = proc;
}

static void fx_renderer_handle_destroy(struct wlr_addon *addon) {
	struct fx_renderer *renderer =
		wl_container_of(addon, renderer, addon);
	fx_renderer_fini(renderer);
	free(renderer);
}
static const struct wlr_addon_interface fx_renderer_addon_impl = {
	.name = "fx_renderer",
	.destroy = fx_renderer_handle_destroy,
};

void fx_renderer_init_addon(struct wlr_egl *egl, struct wlr_addon_set *addons,
		const void * owner) {
	struct fx_renderer *renderer = fx_renderer_create(egl);
	if (!renderer) {
		wlr_log(WLR_ERROR, "Failed to create fx_renderer");
		abort();
	}
	wlr_addon_init(&renderer->addon, addons, owner, &fx_renderer_addon_impl);
}

struct fx_renderer *fx_renderer_addon_find(struct wlr_addon_set *addons,
		const void * owner) {
	struct wlr_addon *addon =
		wlr_addon_find(addons, owner, &fx_renderer_addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct fx_renderer *renderer = wl_container_of(addon, renderer, addon);
	return renderer;
}

struct fx_renderer *fx_renderer_create(struct wlr_egl *egl) {
	struct fx_renderer *renderer = calloc(1, sizeof(struct fx_renderer));
	if (renderer == NULL) {
		return NULL;
	}

	if (!eglMakeCurrent(wlr_egl_get_display(egl), EGL_NO_SURFACE, EGL_NO_SURFACE,
			wlr_egl_get_context(egl))) {
		wlr_log(WLR_ERROR, "GLES2 RENDERER: Could not make EGL current");
		return NULL;
	}

	// get extensions
	const char *exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (exts_str == NULL) {
		wlr_log(WLR_ERROR, "GLES2 RENDERER: Failed to get GL_EXTENSIONS");
		return NULL;
	}

	wlr_log(WLR_INFO, "Creating scenefx GLES2 renderer");
	wlr_log(WLR_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(WLR_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
	wlr_log(WLR_INFO, "GL renderer: %s", glGetString(GL_RENDERER));
	wlr_log(WLR_INFO, "Supported GLES2 extensions: %s", exts_str);

	// TODO: the rest of the gl checks
	if (check_gl_ext(exts_str, "GL_OES_EGL_image_external")) {
		renderer->exts.OES_egl_image_external = true;
		load_gl_proc(&renderer->procs.glEGLImageTargetTexture2DOES,
			"glEGLImageTargetTexture2DOES");
	}

	// quad fragment shader
	if (!link_quad_program(&renderer->shaders.quad)) {
		goto error;
	}
	// fragment shaders
	if (!link_tex_program(&renderer->shaders.tex_rgba, SHADER_SOURCE_TEXTURE_RGBA)) {
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_rgbx, SHADER_SOURCE_TEXTURE_RGBX)) {
		goto error;
	}
	if (!link_tex_program(&renderer->shaders.tex_ext, SHADER_SOURCE_TEXTURE_EXTERNAL)) {
		goto error;
	}

	if (!eglMakeCurrent(wlr_egl_get_display(egl),
				EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
		wlr_log(WLR_ERROR, "GLES2 RENDERER: Could not unset current EGL");
		goto error;
	}

	wlr_log(WLR_INFO, "GLES2 RENDERER: Shaders Initialized Successfully");
	return renderer;

error:
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);

	if (!eglMakeCurrent(wlr_egl_get_display(egl),
				EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
		wlr_log(WLR_ERROR, "GLES2 RENDERER: Could not unset current EGL");
	}

	// TODO: more freeing?
	free(renderer);

	wlr_log(WLR_ERROR, "GLES2 RENDERER: Error Initializing Shaders");
	return NULL;
}

void fx_renderer_fini(struct fx_renderer *renderer) {
	// NO OP
}

void fx_renderer_begin(struct fx_renderer *renderer, int width, int height) {
	glViewport(0, 0, width, height);

	// refresh projection matrix
	matrix_projection(renderer->projection, width, height,
		WL_OUTPUT_TRANSFORM_FLIPPED_180);

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void fx_renderer_clear(const float color[static 4]) {
	glClearColor(color[0], color[1], color[2], color[3]);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void fx_renderer_scissor(struct wlr_box *box) {
	if (box) {
		glScissor(box->x, box->y, box->width, box->height);
		glEnable(GL_SCISSOR_TEST);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
}

bool fx_render_subtexture_with_matrix(struct fx_renderer *renderer,
		struct wlr_texture *wlr_texture, const struct wlr_fbox *src_box,
		const struct wlr_box *dst_box, const float matrix[static 9],
		struct decoration_data *deco_data) {

	assert(wlr_texture_is_gles2(wlr_texture));
	struct wlr_gles2_texture_attribs texture_attrs;
	wlr_gles2_texture_get_attribs(wlr_texture, &texture_attrs);

	struct tex_shader *shader = NULL;

	switch (texture_attrs.target) {
	case GL_TEXTURE_2D:
		if (texture_attrs.has_alpha) {
			shader = &renderer->shaders.tex_rgba;
		} else {
			shader = &renderer->shaders.tex_rgbx;
		}
		break;
	case GL_TEXTURE_EXTERNAL_OES:
		shader = &renderer->shaders.tex_ext;

		if (!renderer->exts.OES_egl_image_external) {
			wlr_log(WLR_ERROR, "Failed to render texture: "
				"GL_TEXTURE_EXTERNAL_OES not supported");
			return false;
		}
		break;
	default:
		wlr_log(WLR_ERROR, "Aborting render");
		abort();
	}

	float gl_matrix[9];
	wlr_matrix_multiply(gl_matrix, renderer->projection, matrix);

	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	wlr_matrix_transpose(gl_matrix, gl_matrix);

	// if there's no opacity or rounded corners we don't need to blend
	if (!texture_attrs.has_alpha && deco_data->alpha == 1.0 && !deco_data->corner_radius) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}

	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture_attrs.target, texture_attrs.tex);

	glTexParameteri(texture_attrs.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glUseProgram(shader->program);

	glUniformMatrix3fv(shader->proj, 1, GL_FALSE, gl_matrix);
	glUniform1i(shader->tex, 0);
	glUniform2f(shader->size, dst_box->width, dst_box->height);
	glUniform2f(shader->position, dst_box->x, dst_box->y);
	glUniform1f(shader->alpha, deco_data->alpha);
	glUniform1f(shader->radius, deco_data->corner_radius);

	const GLfloat x1 = src_box->x / wlr_texture->width;
	const GLfloat y1 = src_box->y / wlr_texture->height;
	const GLfloat x2 = (src_box->x + src_box->width) / wlr_texture->width;
	const GLfloat y2 = (src_box->y + src_box->height) / wlr_texture->height;
	const GLfloat texcoord[] = {
		x2, y1, // top right
		x1, y1, // top left
		x2, y2, // bottom right
		x1, y2, // bottom left
	};

	glVertexAttribPointer(shader->pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(shader->tex_attrib, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(shader->pos_attrib);
	glEnableVertexAttribArray(shader->tex_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(shader->pos_attrib);
	glDisableVertexAttribArray(shader->tex_attrib);

	glBindTexture(texture_attrs.target, 0);

	return true;
}

void fx_render_rect(struct fx_renderer *renderer, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	if (box->width == 0 || box->height == 0) {
		return;
	}
	assert(box->width > 0 && box->height > 0);
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0, projection);

	float gl_matrix[9];
	wlr_matrix_multiply(gl_matrix, renderer->projection, matrix);

	// TODO: investigate why matrix is flipped prior to this cmd
	// wlr_matrix_multiply(gl_matrix, flip_180, gl_matrix);

	wlr_matrix_transpose(gl_matrix, gl_matrix);

	if (color[3] == 1.0) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}

	struct quad_shader shader = renderer->shaders.quad;
	glUseProgram(shader.program);

	glUniformMatrix3fv(shader.proj, 1, GL_FALSE, gl_matrix);
	glUniform4f(shader.color, color[0], color[1], color[2], color[3]);

	glVertexAttribPointer(shader.pos_attrib, 2, GL_FLOAT, GL_FALSE,
			0, verts);

	glEnableVertexAttribArray(shader.pos_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(shader.pos_attrib);
}
