#include "scenefx/render/fx_renderer/fx_blur.h"

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <wlr/util/log.h>

#include "render/fx_renderer/fx_renderer.h"
#include "scenefx/render/pass.h"
#include "scenefx/render/fx_renderer/fx_renderer.h"

struct fx_blur_plugin_info *fx_blur_plugin_create(char *path, char *id) {
	struct fx_blur_plugin_info *data = malloc(sizeof(*data));
	if (!data) {
		return NULL;
	}

	char *path_copy = malloc(strlen(path) + 1);
	if (!path_copy) {
		free(data);
		return NULL;
	}

	char *id_copy = malloc(strlen(id) + 1);
	if (!id_copy) {
		free(data);
		free(path_copy);
		return NULL;
	}

	strcpy(path_copy, path);
	strcpy(id_copy, id);
	data->path = path_copy;
	data->id = id_copy;
	return data;
}
void fx_blur_plugin_destroy(struct fx_blur_plugin_info *info) {
	if (info->state & BLUR_LOADED) {
		info->blur_impl->destroy(info->user_data);
	}
	free(info->id);
	free(info->path);
	free(info);
}

bool fx_blur_plugin_load(struct fx_blur_plugin_info *info) {
	if (info->state & BLUR_LOADED) {
		return true;
	}

	info->handle = dlopen(info->path, RTLD_LAZY);
	if (!info->handle) {
		char *err = dlerror();
		wlr_log(WLR_ERROR, "failed to load blur plugin %s (from %s): %s", info->id, info->path, err);
		return false;
	}

	const fx_blur_get_by_id_func by_id_func = dlsym(info->handle, "fx_blur_by_id");
	if (!by_id_func) {
		char *err = dlerror();
		wlr_log(WLR_ERROR, "failed to load blur plugin %s (from %s): couldn't load 'fx_blur_by_id' function: %s", info->id, info->path, err);
		return false;
	}
	const struct fx_blur_impl *blur_impl = by_id_func(info->id);
	if (blur_impl == NULL) {
		wlr_log(WLR_ERROR, "failed to load blur plugin %s (from %s): no blur exists with that id in plugin", info->id, info->path);
		return false;
	}

	if (blur_impl->execute == NULL || blur_impl->init == NULL || blur_impl->name == NULL) {
		wlr_log(WLR_ERROR, "failed to load blur plugin %s (from %s): not all required fields initialized (execute, init, and name)", info->id, info->path);
		return false;
	}

	info->blur_impl = blur_impl;
	info->state = BLUR_LOADED;
	return true;
}

bool fx_blur_plugin_link(struct wlr_renderer *renderer, struct fx_blur_plugin_info *info) {
	if (info->state & BLUR_LINKED) {
		return true;
	}

	if (!(info->state & BLUR_LOADED) || !info->blur_impl) {
		wlr_log(WLR_ERROR, "trying to link blur plugin before loading");
		return false;
	}

	struct fx_renderer *fx_renderer = fx_get_renderer(renderer);


	if (!wlr_egl_make_current(fx_renderer->egl, NULL)) {
		return false;
	}

	EGLint client_version;
	eglQueryContext(fx_renderer->egl->display, fx_renderer->egl->context,
		EGL_CONTEXT_CLIENT_VERSION, &client_version);

	bool res = info->blur_impl->init(client_version, info->id, &info->user_data);
	if (!res) {
		return false;
	}

	wlr_egl_unset_current(fx_renderer->egl);

	info->state |= BLUR_LINKED;
	return true;
}

struct fx_blur_plugin_info *fx_renderer_get_plugin(struct wlr_renderer *renderer, char *id) {
	struct fx_renderer *fx_renderer = fx_get_renderer(renderer);
	struct fx_blur_plugin_info *item;
	wl_list_for_each(item, &fx_renderer->blur_shader_data, link) {
		if (strcmp(id, item->id) == 0) {
			return item;
		}
	}

	return NULL;
}

void fx_renderer_add_plugin(struct wlr_renderer *renderer, struct fx_blur_plugin_info *info) {
	struct fx_renderer *fx_renderer = fx_get_renderer(renderer);
	wl_list_insert(&fx_renderer->blur_shader_data, &info->link);
}

void fx_renderer_remove_plugin(struct wlr_renderer *renderer, struct fx_blur_plugin_info *info) {
	wl_list_remove(&info->link);
}
