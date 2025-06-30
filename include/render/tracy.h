#ifndef TRACY
#define TRACY

#include "include/render/fx_renderer/fx_renderer.h"

struct tracy_data;
struct tracy_gpu_ctx {
	struct tracy_data *tracy_data;
	bool is_active;
};

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>

#define TRACY_MARK_FRAME_START(name) TracyCFrameMarkStart(name)
#define TRACY_MARK_FRAME_END(name) TracyCFrameMarkEnd(name)

#define TRACY_ZONE_START \
	TracyCZoneS(ctx, 10, true)
#define TRACY_ZONE_END \
	TracyCZoneEnd(ctx)
#define TRACY_ZONE_VALUE(value) \
	TracyCZoneValue(ctx, value);

#define TRACY_GPU_ZONE_START_NAME(renderer, name) \
	struct tracy_gpu_ctx gpu_ctx; \
	tracy_gpu_zone_begin(renderer->tracy_data, &gpu_ctx, __LINE__, __FILE__, __func__, name)
#define TRACY_GPU_ZONE_START(renderer) \
	struct tracy_gpu_ctx gpu_ctx; \
	tracy_gpu_zone_begin(renderer->tracy_data, &gpu_ctx, __LINE__, __FILE__, __func__, __func__)
#define TRACY_GPU_ZONE_END \
	tracy_gpu_zone_end(&gpu_ctx)
#define TRACY_GPU_ZONE_COLLECT(renderer) \
	tracy_gpu_context_collect(renderer->tracy_data)

#define TRACY_GPU_CONTEXT_NEW(renderer) \
	tracy_gpu_context_new(renderer)
#define TRACY_GPU_CONTEXT_DESTROY(tracy_data) \
	tracy_gpu_context_destroy(tracy_data);

#define TRACY_BOTH_ZONES_START(renderer) \
	TRACY_ZONE_START \
	TRACY_GPU_ZONE_START(renderer)
#define TRACY_BOTH_ZONES_END \
	TRACY_ZONE_END \
	TRACY_GPU_ZONE_END

// Private functions: Don't use these outside of this file!

void tracy_gpu_zone_begin(struct tracy_data *tracy_data, struct tracy_gpu_ctx *out_ctx,
		const int line, const char *source, const char *func, const char *name);
void tracy_gpu_zone_end(struct tracy_gpu_ctx *ctx);

void tracy_gpu_context_collect(struct tracy_data *tracy_data);
void tracy_gpu_context_destroy(struct tracy_data *tracy_data);
struct tracy_data *tracy_gpu_context_new(struct fx_renderer *renderer);

#else

#define TRACY_MARK_FRAME_START(name)
#define TRACY_MARK_FRAME_END(name)

#define TRACY_ZONE_START
#define TRACY_ZONE_END
#define TRACY_ZONE_VALUE(value)

#define TRACY_GPU_ZONE_START_NAME(renderer, name)
#define TRACY_GPU_ZONE_START(renderer)
#define TRACY_GPU_ZONE_END
#define TRACY_GPU_ZONE_COLLECT(renderer)

#define TRACY_GPU_CONTEXT_NEW(renderer) NULL
#define TRACY_GPU_CONTEXT_DESTROY(tracy_data)

#define TRACY_BOTH_ZONES_START(renderer)
#define TRACY_BOTH_ZONES_END

#endif  /* ifdef TRACY_ENABLE */

#endif  // !TRACY
