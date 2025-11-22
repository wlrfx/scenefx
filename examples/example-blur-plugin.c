#include <string.h>
#include "example-blur-plugin.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <GLES2/gl2.h>
#include <wlr/util/log.h>

#include "scenefx/render/pass.h"
#include "scenefx/render/fx_renderer/fx_blur.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"

struct invert_data
{
	struct fx_blur_plugin_base_shader base;
	GLint target_offset;
	GLint target_size;
	GLint source_size;
};

static  char* invert_gles3_src =
	"#version 300 es\n"
	"\n"
	"precision highp float;\n"
	"\n"
	"in vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"uniform vec2 target_offset;\n"
	"uniform vec2 target_size;\n"
	"uniform vec2 source_size;\n"
	"out vec4 fragColor;\n"
	"\n"
	"void main() {\n"
	"	vec2 full = v_texcoord;\n"
	"	vec2 offset = target_offset / source_size;\n"
	"	vec2 limit = target_size / source_size;\n"
	"	vec2 inverted_coord = (limit - (v_texcoord - offset)) + offset;\n"
	"	fragColor = texture(tex, inverted_coord);\n"
	"}\n";

static bool invert_init(int client_version, void** user_data)
{
	struct invert_data* v = malloc(sizeof(struct invert_data));
	if (!v)
	{
		return false;
	}

	if (!fx_link_blur_plugin_base_shader_program(&v->base, invert_gles3_src, client_version))
	{
		wlr_log(WLR_ERROR, "failed to link blur plugin");
		free(v);
		return false;
	}

	v->source_size = glGetUniformLocation(v->base.program, "source_size");
	v->target_size = glGetUniformLocation(v->base.program, "target_size");
	v->target_offset = glGetUniformLocation(v->base.program, "target_offset");
	*user_data = v;

	return true;
}

static void invert_execute(pixman_region32_t* damage, struct wlr_box* box,
                           struct fx_gles_render_pass* pass,
                           struct fx_render_blur_pass_options* fx_options,
                           void* data)
{
	struct fx_render_texture_options* tex_options = &fx_options->tex_options;
	struct wlr_render_texture_options* options = &tex_options->base;
	struct fx_texture* texture = fx_get_texture(options->texture);

	struct invert_data shader = *(struct invert_data*)data;

	struct wlr_box dst_box;
	struct wlr_fbox src_fbox;
	wlr_render_texture_options_get_src_box(options, &src_fbox);
	wlr_render_texture_options_get_dst_box(options, &dst_box);

	src_fbox.x /= options->texture->width;
	src_fbox.y /= options->texture->height;
	src_fbox.width /= options->texture->width;
	src_fbox.height /= options->texture->height;

	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);

	glUseProgram(shader.base.program);

	glActiveTexture(GL_TEXTURE0);

	GLuint tex_target = fx_texture_get_target(texture);
	GLuint tex_tex = fx_texture_get_texture(texture);

	glBindTexture(tex_target, tex_tex);

	switch (options->filter_mode)
	{
	case WLR_SCALE_FILTER_BILINEAR:
		glTexParameteri(tex_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(tex_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	case WLR_SCALE_FILTER_NEAREST:
		abort();
	}

	glUniform1i(shader.base.tex, 0);

	glUniform2f(shader.target_size, box->width, box->height);
	glUniform2f(shader.target_offset, box->x, box->y);
	glUniform2f(shader.source_size, options->texture->width,
	            options->texture->height);

	fx_set_proj_matrix(shader.base.proj, pass->projection_matrix, &dst_box);
	fx_set_tex_matrix(shader.base.tex_proj, options->transform, &src_fbox);

	fx_render(&dst_box, options->clip, shader.base.pos_attrib);

	glBindTexture(tex_target, 0);

	wlr_texture_destroy(options->texture);
}

static void invert_destroy(void* data)
{
	struct invert_data shader = *(struct invert_data*)data;
	free(data);
	glDeleteProgram(shader.base.program);
}

static const struct fx_blur_impl invert = {
	.name = "invert",
	.init = invert_init,
	.get_expansion = NULL,
	.prepare = NULL,
	.execute = invert_execute,
	.destroy = invert_destroy,
};

const struct fx_blur_impl* fx_blur_by_id(char* name)
{
	if (strcmp(invert.name, name) == 0)
	{
		return &invert;
	}

	return NULL;
}
