#ifdef TRACY_ENABLE
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tracy/TracyC.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "render/gles2/gles2.h"
#include "render/tracy.h"

static atomic_int id_counter = 0;

static void gles2_tracy_gpu_zone_begin(struct fx_renderer *fx_renderer, struct tracy_data *tracy_data,
		struct tracy_gpu_zone_context *out_ctx, const int line,
		const char *source, const char *func, const char *name) {
	struct gles2_renderer *gles2_renderer = gles2_get_renderer(fx_renderer);

	out_ctx->tracy_data = tracy_data;
#ifdef TRACY_ON_DEMAND
	out_ctx->is_active = TracyCIsConnected;
#else
	out_ctx->is_active = true;
#endif

	if (!out_ctx->is_active) {
		return;
	}

	// Create the query object
	const unsigned int query = tracy_get_next_query_index(tracy_data);
	gles2_renderer->gl_procs.glQueryCounterEXT(tracy_data->queue.queries[query], GL_TIMESTAMP_EXT);

	// Label the zone
	uint64_t srcloc = ___tracy_alloc_srcloc_name(line,
			source, strlen(source),
			func, strlen(func),
			name, strlen(name),
			0);

	// Begin the Tracy GPU zone
	const struct ___tracy_gpu_zone_begin_data data = {
		.context = tracy_data->context_id,
		.queryId = (uint16_t) query,
		.srcloc = srcloc,
	};
	___tracy_emit_gpu_zone_begin_alloc(data);
}

static void gles2_tracy_gpu_zone_end(struct fx_renderer *fx_renderer, struct tracy_gpu_zone_context *ctx) {
	struct gles2_renderer *gles2_renderer = gles2_get_renderer(fx_renderer);
	struct tracy_data *tracy_data = ctx->tracy_data;

	// Create the query object
	const unsigned int query = tracy_get_next_query_index(tracy_data);
	gles2_renderer->gl_procs.glQueryCounterEXT(tracy_data->queue.queries[query], GL_TIMESTAMP_EXT);

	// End the Tracy GPU zone
	const struct ___tracy_gpu_zone_end_data data = {
		.context = tracy_data->context_id,
		.queryId = (uint16_t) query,
	};
	___tracy_emit_gpu_zone_end(data);
}

static void gles2_tracy_gpu_context_collect(struct fx_renderer *fx_renderer, struct tracy_data *tracy_data) {
	struct gles2_renderer *gles2_renderer = gles2_get_renderer(fx_renderer);

	if (tracy_data->queue.tail == tracy_data->queue.head) {
		return;
	}

#ifdef TRACY_ON_DEMAND
	if (!TracyCIsConnected) {
		tracy_data->queue.head = 0;
		tracy_data->queue.tail = 0;
		return;
	}
#endif

	while (tracy_data->queue.tail != tracy_data->queue.head) {
		GLint available;
		gles2_renderer->gl_procs.glGetQueryObjectivEXT(
				tracy_data->queue.queries[tracy_data->queue.tail],
				GL_QUERY_RESULT_AVAILABLE_EXT, &available);
		if (!available) {
			return;
		}

		GLuint64 time;
		gles2_renderer->gl_procs.glGetQueryObjectui64vEXT(
				tracy_data->queue.queries[tracy_data->queue.tail],
				GL_QUERY_RESULT_EXT, &time);

		const struct ___tracy_gpu_time_data data = {
			.context = tracy_data->context_id,
			.gpuTime = time,
			.queryId = (uint16_t) tracy_data->queue.tail,
		};
		___tracy_emit_gpu_time(data);

		tracy_data->queue.tail = (tracy_data->queue.tail + 1) % QUERY_QUEUE_LEN;
	}
}

static void gles2_tracy_gpu_context_destroy(struct fx_renderer *fx_renderer, struct tracy_data *tracy_data) {
	struct gles2_renderer *gles2_renderer = gles2_get_renderer(fx_renderer);

	gles2_renderer->gl_procs.glDeleteQueriesEXT(QUERY_QUEUE_LEN, tracy_data->queue.queries);
}

static struct tracy_data *gles2_tracy_gpu_context_new(struct fx_renderer *fx_renderer) {
	struct gles2_renderer *gles2_renderer = gles2_get_renderer(fx_renderer);

	struct tracy_data *tracy_data = calloc(1, sizeof(*tracy_data));
	if (tracy_data == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	tracy_data->fx_renderer = fx_renderer;
	tracy_data->context_id = ++id_counter;
	tracy_data->queue.head = 0;
	tracy_data->queue.tail = 0;

	// Create the query objects
	gles2_renderer->gl_procs.glGenQueriesEXT(QUERY_QUEUE_LEN, tracy_data->queue.queries);

	// Get the current GL time
	GLint64 gl_gpu_time;
	gles2_renderer->gl_procs.glGetInteger64vEXT(GL_TIMESTAMP_EXT, &gl_gpu_time);

	// Create the query objects
	GLint bits;
	gles2_renderer->gl_procs.glGetQueryivEXT(GL_TIMESTAMP_EXT, GL_QUERY_COUNTER_BITS_EXT, &bits);

	const struct ___tracy_gpu_new_context_data data = {
		.context = tracy_data->context_id,
		.gpuTime = gl_gpu_time,
		.period = 1.0f,
		.flags = 0,
		.type = 1, // `enum class GpuContextType` in TracyQueue.hpp
	};
	___tracy_emit_gpu_new_context(data);

	// Set the custom name
	int len = snprintf(NULL, 0, "FX Renderer (GLESV2): %s",
		 glGetString(GL_RENDERER)) + 1;
	char ctx_name[len];
	snprintf(ctx_name, len, "FX Renderer (GLESV2): %s",
		glGetString(GL_RENDERER));
	const struct ___tracy_gpu_context_name_data name_data = {
		.context = tracy_data->context_id,
		.name = ctx_name,
		.len = strlen(ctx_name),
	};
	___tracy_emit_gpu_context_name(name_data);

	return tracy_data;
}

const struct fx_renderer_tracy_impl gles2_renderer_tracy_impl = {
	.gpu_zone_begin = gles2_tracy_gpu_zone_begin,
	.gpu_zone_end = gles2_tracy_gpu_zone_end,
	.gpu_context_collect = gles2_tracy_gpu_context_collect,
	.gpu_context_destroy = gles2_tracy_gpu_context_destroy,
	.gpu_context_new = gles2_tracy_gpu_context_new,
};

#endif
