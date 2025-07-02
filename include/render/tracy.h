#ifndef TRACY
#define TRACY

#include <stdio.h>

#include "include/render/fx_renderer/fx_renderer.h"

struct tracy_data;
struct tracy_gpu_zone_context {
	struct tracy_data *tracy_data;
	bool is_active;
};

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>

/**
 * Frame
 */

#define TRACY_MARK_FRAME_START(name) TracyCFrameMarkStart(name)
#define TRACY_MARK_FRAME_END(name) TracyCFrameMarkEnd(name)

/**
 * Zone
 */

#define TRACY_ZONE_START \
	TracyCZone(ctx, true)
#define TRACY_ZONE_START_N(name) \
	TracyCZoneN(ctx, name, true)
#define TRACY_ZONE_END \
	TracyCZoneEnd(ctx)

/**
 * Zone Helpers
 */

#define TRACY_ZONE_TEXT(txt, size) \
	TracyCZoneText(ctx, txt, size);
#define TRACY_ZONE_NAME(txt, size) \
	TracyCZoneName(ctx, txt, size);
#define TRACY_ZONE_NAME_f(...) \
	{ \
		int len = snprintf(NULL, 0, __VA_ARGS__) + 1; \
		char str[len]; \
		snprintf(str, len, __VA_ARGS__); \
		TracyCZoneName(ctx, str, len); \
	}
#define TRACY_ZONE_COLOR(color) \
	TracyCZoneColor(ctx, color);
#define TRACY_ZONE_VALUE(value) \
	TracyCZoneValue(ctx, value);

/**
 * GPU Zone
 */

#define TRACY_GPU_ZONE_START_NAME(renderer, name) \
	struct tracy_gpu_zone_context gpu_ctx; \
	tracy_gpu_zone_begin(renderer->tracy_data, &gpu_ctx, __LINE__, __FILE__, __func__, name)
#define TRACY_GPU_ZONE_START(renderer) \
	struct tracy_gpu_zone_context gpu_ctx; \
	tracy_gpu_zone_begin(renderer->tracy_data, &gpu_ctx, __LINE__, __FILE__, __func__, __func__)
#define TRACY_GPU_ZONE_END \
	tracy_gpu_zone_end(&gpu_ctx)
#define TRACY_GPU_ZONE_COLLECT(renderer) \
	tracy_gpu_context_collect(renderer->tracy_data)

/**
 * GPU Context
 */

#define TRACY_GPU_CONTEXT_NEW(renderer) \
	tracy_gpu_context_new(renderer)
#define TRACY_GPU_CONTEXT_DESTROY(tracy_data) \
	tracy_gpu_context_destroy(tracy_data);

/**
 * GPU/CPU Zones Helpers
 */

#define TRACY_BOTH_ZONES_START(renderer) \
	TRACY_ZONE_START \
	TRACY_GPU_ZONE_START(renderer)
#define TRACY_BOTH_ZONES_END \
	TRACY_ZONE_END \
	TRACY_GPU_ZONE_END

// Private functions: Don't use these outside of this file!

void tracy_gpu_zone_begin(struct tracy_data *tracy_data, struct tracy_gpu_zone_context *out_ctx,
		const int line, const char *source, const char *func, const char *name);
void tracy_gpu_zone_end(struct tracy_gpu_zone_context *ctx);

void tracy_gpu_context_collect(struct tracy_data *tracy_data);
void tracy_gpu_context_destroy(struct tracy_data *tracy_data);
struct tracy_data *tracy_gpu_context_new(struct fx_renderer *renderer);

#else

/**
 * Frame
 */

#define TRACY_MARK_FRAME_START(name)
#define TRACY_MARK_FRAME_END(name)

/**
 * Zone
 */

#define TRACY_ZONE_START
#define TRACY_ZONE_START_N(name)
#define TRACY_ZONE_END

/**
 * Zone helpers
 */

#define TRACY_ZONE_TEXT(txt, size)
#define TRACY_ZONE_NAME(txt, size)
#define TRACY_ZONE_NAME_f(...)
#define TRACY_ZONE_COLOR(color)
#define TRACY_ZONE_VALUE(value)

/**
 * GPU Zone
 */

#define TRACY_GPU_ZONE_START_NAME(renderer, name)
#define TRACY_GPU_ZONE_START(renderer)
#define TRACY_GPU_ZONE_END
#define TRACY_GPU_ZONE_COLLECT(renderer)

/**
 * GPU Context
 */

#define TRACY_GPU_CONTEXT_NEW(renderer) NULL
#define TRACY_GPU_CONTEXT_DESTROY(tracy_data)

/**
 * GPU/CPU Zones Helpers
 */

#define TRACY_BOTH_ZONES_START(renderer)
#define TRACY_BOTH_ZONES_END

#endif  /* ifdef TRACY_ENABLE */

#endif  // !TRACY
