#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/addon.h>
#include <wlr/util/log.h>

#include "scenefx/render/fx_renderer/fx_effect_framebuffers.h"

static void addon_handle_destroy(struct wlr_addon *addon) {
	struct fx_effect_framebuffers *fbos = wl_container_of(addon, fbos, addon);
	wlr_addon_finish(&fbos->addon);
	free(fbos);
}

static const struct wlr_addon_interface fbos_addon_impl = {
	.name = "fx_effect_framebuffers",
	.destroy = addon_handle_destroy,
};

static bool fx_effect_framebuffers_assign(struct wlr_output *output,
		struct fx_effect_framebuffers *fbos) {
	wlr_addon_init(&fbos->addon, &output->addons, output, &fbos_addon_impl);
	return true;
}

struct fx_effect_framebuffers *fx_effect_framebuffers_try_get(struct wlr_output *output) {
	struct fx_effect_framebuffers *fbos = NULL;
	if (!output) {
		return NULL;
	}

	struct wlr_addon *addon = wlr_addon_find(&output->addons, output,
			&fbos_addon_impl);
	if (!addon) {
		goto create_new;
	}

	if (!(fbos = wl_container_of(addon, fbos, addon))) {
		goto create_new;
	}
	return fbos;

create_new:;
	fbos = calloc(1, sizeof(*fbos));
	if (!fbos) {
		wlr_log(WLR_ERROR, "Could not allocate a fx_effect_framebuffers");
		return NULL;
	}

	if (!fx_effect_framebuffers_assign(output, fbos)) {
		wlr_log(WLR_ERROR, "Could not assign fx_effect_framebuffers to output: '%s'",
				output->name);
		free(fbos);
		return NULL;
	}
	return fbos;
}
