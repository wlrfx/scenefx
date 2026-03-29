#include <stdio.h>
#ifdef TRACY_ENABLE
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <tracy/TracyC.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "render/fx_renderer.h"
#include "render/tracy.h"

// Docs used in the implementation:
// - https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf
// - https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt
// - https://github.com/wolfpld/tracy/blob/7388a476587c93f4e8e3e50d64c1400fc8c5e47e/public/tracy/TracyOpenGL.hpp

// Colors:
//	From https://github.com/wolfpld/tracy/blob/7388a476587c93f4e8e3e50d64c1400fc8c5e47e/public/common/TracyColor.hpp
#define RED4 0x8b0000

/*
 * GPU Zone
 */

unsigned int tracy_get_next_query_index(struct tracy_data *tracy_data) {
	const unsigned int id = tracy_data->queue.head;
	tracy_data->queue.head = (tracy_data->queue.head + 1) % QUERY_QUEUE_LEN;
	assert(tracy_data->queue.head != tracy_data->queue.tail);
	return id;
}

void tracy_gpu_zone_begin(struct tracy_data *tracy_data, struct tracy_gpu_zone_context *out_ctx,
		const int line, const char *source, const char *func, const char *name) {
	*out_ctx = (struct tracy_gpu_zone_context) { 0 };
	if (out_ctx == NULL || tracy_data == NULL) {
		return;
	}
	if (!tracy_data->fx_renderer || !tracy_data->fx_renderer->tracy_capable) {
		return;
	}
	struct fx_renderer *fx_renderer = tracy_data->fx_renderer;
	fx_renderer->impl->tracy_gpu_zone_begin(fx_renderer, tracy_data, out_ctx, line, source, func, name);
}

void tracy_gpu_zone_end(struct tracy_gpu_zone_context *ctx) {
	if (ctx == NULL || ctx->tracy_data == NULL || !ctx->is_active) {
		return;
	}
	struct tracy_data *tracy_data = ctx->tracy_data;
	if (tracy_data == NULL || tracy_data->fx_renderer == NULL
			|| !tracy_data->fx_renderer->tracy_capable) {
		return;
	}
	struct fx_renderer *fx_renderer = tracy_data->fx_renderer;
	fx_renderer->impl->tracy_gpu_zone_end(fx_renderer, ctx);
}

void tracy_gpu_context_collect(struct tracy_data *tracy_data) {
	if (tracy_data == NULL) {
		TracyCMessageL("tracy_data == NULL");
		return;
	}
	if (!tracy_data->fx_renderer || !tracy_data->fx_renderer->tracy_capable) {
		return;
	}

	TracyCZoneC(ctx, RED4, true);

	struct fx_renderer *fx_renderer = tracy_data->fx_renderer;
	fx_renderer->impl->tracy_gpu_context_collect(fx_renderer, tracy_data);

	TracyCZoneEnd(ctx);
}

void tracy_gpu_context_destroy(struct tracy_data *tracy_data) {
	if (tracy_data == NULL) {
		return;
	}
	if (!tracy_data->fx_renderer || !tracy_data->fx_renderer->tracy_capable) {
		return;
	}

	struct fx_renderer *fx_renderer = tracy_data->fx_renderer;
	fx_renderer->impl->tracy_gpu_context_destroy(fx_renderer, tracy_data);

	free(tracy_data);
}

struct tracy_data *tracy_gpu_context_new(struct fx_renderer *fx_renderer) {
	assert(fx_renderer);
	if (!fx_renderer->tracy_capable) {
		wlr_log(WLR_ERROR, "FX Renderer not Tracy capable");
		return NULL;
	}

	return fx_renderer->impl->tracy_gpu_context_new(fx_renderer);
}
#endif /* ifdef TRACY_ENABLE */
