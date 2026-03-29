#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/addon.h>
#include <wlr/util/log.h>

#include "render/fx_renderer.h"

static void addon_handle_destroy(struct wlr_addon *addon) {
	struct fx_offscreen_buffers *offscreen_buffers = wl_container_of(addon, offscreen_buffers, addon);

	offscreen_buffers->impl->destroy(offscreen_buffers);

	wl_list_remove(&offscreen_buffers->link);
	wlr_addon_finish(&offscreen_buffers->addon);
	free(offscreen_buffers);
}

static const struct wlr_addon_interface offscreen_buffers_addon_impl = {
	.name = "fx_offscreen_buffers",
	.destroy = addon_handle_destroy,
};

void fx_offscreen_buffers_destroy(struct fx_offscreen_buffers *offscreen_buffers) {
	addon_handle_destroy(&offscreen_buffers->addon);
}

void fx_offscreen_buffers_init(struct fx_offscreen_buffers *offscreen_buffers,
		const struct fx_offscreen_buffers_impl *impl,
		struct fx_renderer *fx_renderer, struct wlr_output *output) {
	assert(impl->destroy);

	*offscreen_buffers = (struct fx_offscreen_buffers) {
		.impl = impl,
		.fx_renderer = fx_renderer,
		.wlr_output = output,
	};
	wlr_addon_init(&offscreen_buffers->addon, &output->addons, output, &offscreen_buffers_addon_impl);
	wl_list_insert(&fx_renderer->offscreen_buffers, &offscreen_buffers->link);
}

struct fx_offscreen_buffers *fx_offscreen_buffers_try_get(struct fx_renderer *fx_renderer,
		struct wlr_output *output) {
	struct fx_offscreen_buffers *offscreen_buffers = NULL;
	if (!output) {
		return NULL;
	}

	struct wlr_addon *addon = wlr_addon_find(&output->addons, output,
			&offscreen_buffers_addon_impl);
	if (!addon) {
		goto create_new;
	}

	if (!(offscreen_buffers = wl_container_of(addon, offscreen_buffers, addon))) {
		goto create_new;
	}
	return offscreen_buffers;

create_new:;
	return fx_renderer->impl->offscreen_buffers_allocate(fx_renderer, output);
}
