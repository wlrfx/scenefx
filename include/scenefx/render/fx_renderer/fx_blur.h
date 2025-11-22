#ifndef SWAYFX_FX_BLUR_H
#define SWAYFX_FX_BLUR_H
#include <stdbool.h>
#include <wayland-util.h>
#include <GLES2/gl2.h>

enum fx_blur_shader_state {
	BLUR_NONE = 0,
	BLUR_LOADED = 1,
	BLUR_LINKED = 2,
	BLUR_READY = BLUR_LINKED | BLUR_LOADED,
};

struct fx_blur_plugin_info {
	struct wl_list link;
	enum fx_blur_shader_state state;
	void *handle;
	char *path;
	char *id;
	const struct fx_blur_impl *blur_impl;
	void *user_data;
};


struct fx_blur_plugin_base_shader {
	GLuint program;
	GLint proj;
	GLint tex_proj;
	GLint tex;
	GLint pos_attrib;
};

bool fx_link_blur_plugin_base_shader_program(
	struct fx_blur_plugin_base_shader *shader,
	char *shader_src,
	GLint client_version
);

struct wlr_renderer;
struct fx_blur_plugin_info *fx_blur_plugin_create(char *path, char *id);
void fx_blur_plugin_destroy(struct fx_blur_plugin_info *info);
bool fx_blur_plugin_load(struct fx_blur_plugin_info *info);
bool fx_blur_plugin_link(struct wlr_renderer *renderer, struct fx_blur_plugin_info *info);
// TODO:
// void blur_plugin_reload(struct fx_blur_shader_info *info);

struct fx_blur_plugin_info *fx_renderer_get_plugin(struct wlr_renderer *renderer, char *id);
void fx_renderer_add_plugin(struct wlr_renderer *renderer, struct fx_blur_plugin_info *info);
void fx_renderer_remove_plugin(struct wlr_renderer *renderer, struct fx_blur_plugin_info *info);

typedef const struct fx_blur_impl *(*fx_blur_get_by_id_func)(char *);

#endif //SWAYFX_FX_BLUR_H
