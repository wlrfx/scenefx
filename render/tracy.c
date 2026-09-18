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

#define TRACY_AVAILABLE(fx_renderer) \
	((fx_renderer)->impl->tracy != NULL && (fx_renderer)->tracy_data != NULL)

/*
 * GPU Zone
 */

unsigned int tracy_get_next_query_index(struct tracy_data *tracy_data) {
	const unsigned int id = tracy_data->queue.head;
	tracy_data->queue.head = (tracy_data->queue.head + 1) % QUERY_QUEUE_LEN;
	assert(tracy_data->queue.head != tracy_data->queue.tail);
	return id;
}

void tracy_gpu_zone_begin(struct fx_renderer *fx_renderer,
		struct tracy_gpu_zone_context *out_ctx, const int line, const char *source,
		const char *func, const char *name) {
	*out_ctx = (struct tracy_gpu_zone_context) { 0 };
	if (TRACY_AVAILABLE(fx_renderer)) {
		fx_renderer->impl->tracy->gpu_zone_begin(fx_renderer, fx_renderer->tracy_data,
				out_ctx, line, source, func, name);
	}
}

void tracy_gpu_zone_end(struct tracy_gpu_zone_context *ctx) {
	if (ctx->is_active && ctx->tracy_data != NULL
			&& TRACY_AVAILABLE(ctx->tracy_data->fx_renderer)) {
		ctx->tracy_data->fx_renderer->impl->tracy->gpu_zone_end(
				ctx->tracy_data->fx_renderer, ctx);
	}
}

void tracy_gpu_context_collect(struct fx_renderer *fx_renderer) {
	if (!TRACY_AVAILABLE(fx_renderer)) {
		TracyCMessageL("tracy_data == NULL");
		return;
	}

	TracyCZoneC(ctx, RED4, true);

	fx_renderer->impl->tracy->gpu_context_collect(fx_renderer, fx_renderer->tracy_data);

	TracyCZoneEnd(ctx);
}

void tracy_gpu_context_destroy(struct fx_renderer *fx_renderer) {
	if (TRACY_AVAILABLE(fx_renderer)) {
		fx_renderer->impl->tracy->gpu_context_destroy(fx_renderer, fx_renderer->tracy_data);

		free(fx_renderer->tracy_data);
		fx_renderer->tracy_data = NULL;
	}
}

struct tracy_data *tracy_gpu_context_new(struct fx_renderer *fx_renderer) {
	assert(fx_renderer);
	if (fx_renderer->impl->tracy == NULL) {
		wlr_log(WLR_ERROR, "FX Renderer not Tracy capable");
		return NULL;
	}
	return fx_renderer->impl->tracy->gpu_context_new(fx_renderer);
}

#endif /* ifdef TRACY_ENABLE */
