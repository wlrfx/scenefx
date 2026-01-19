#ifdef TRACY_ENABLE
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tracy/TracyC.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "render/tracy.h"

// Docs used in the implementation:
// - https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf
// - https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt
// - https://github.com/wolfpld/tracy/blob/7388a476587c93f4e8e3e50d64c1400fc8c5e47e/public/tracy/TracyOpenGL.hpp

#define QUERY_COUNT (64 * 1024)

#define SCREEN_CAPTURE_COPIES 4
// Has to be % 4 == 0 (tracy compression requirement)
#define SCREEN_CAPTURE_DST_WIDTH 640
#define SCREEN_CAPTURE_DST_HEIGHT 360

// Colors:
//	From https://github.com/wolfpld/tracy/blob/7388a476587c93f4e8e3e50d64c1400fc8c5e47e/public/common/TracyColor.hpp
#define RED4 0x8b0000

static atomic_int id_counter = 0;

struct tracy_capture_data {
	GLuint tex[SCREEN_CAPTURE_COPIES];
	GLuint fbo[SCREEN_CAPTURE_COPIES];
	GLuint pbo[SCREEN_CAPTURE_COPIES];
	GLsync fence[SCREEN_CAPTURE_COPIES];
	int index;

	struct wl_array queue;
};

struct tracy_data {
	struct fx_renderer *renderer;

	uint8_t context_id;

	uint32_t queries[QUERY_COUNT];
	uint32_t q_head;
	uint32_t q_tail;

	struct tracy_capture_data *tracy_capture_data;
};


/*
 * Buffer capture
 */

static struct tracy_capture_data *tracy_capture_init(struct tracy_data *tracy_data) {
	struct tracy_capture_data *data = calloc(1, sizeof(*data));
	if (!data) {
		return NULL;
	}

	data->index = 0;
	wl_array_init(&data->queue);

	glGenTextures(SCREEN_CAPTURE_COPIES, data->tex);
	glGenFramebuffers(SCREEN_CAPTURE_COPIES, data->fbo);
	glGenBuffers(SCREEN_CAPTURE_COPIES, data->pbo);
	for (int i = 0; i < SCREEN_CAPTURE_COPIES; i++) {
		glBindTexture(GL_TEXTURE_2D, data->tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
				SCREEN_CAPTURE_DST_WIDTH, SCREEN_CAPTURE_DST_HEIGHT,
				0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindFramebuffer(GL_FRAMEBUFFER, data->fbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 , GL_TEXTURE_2D,
				data->tex[i], 0);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, data->pbo[i]);
		glBufferData(GL_PIXEL_PACK_BUFFER,
				// NOTE: Assume 4 bytes per pixel
				SCREEN_CAPTURE_DST_WIDTH * SCREEN_CAPTURE_DST_HEIGHT * 4,
				NULL, GL_STREAM_READ);
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	return data;
}

static void tracy_capture_destroy(struct tracy_capture_data *data) {
	if (!data) {
		return;
	}

	wl_array_release(&data->queue);
	glDeleteTextures(SCREEN_CAPTURE_COPIES, data->tex);
	glDeleteFramebuffers(SCREEN_CAPTURE_COPIES, data->fbo);
	glDeleteBuffers(SCREEN_CAPTURE_COPIES, data->pbo);
	for (size_t i = 0; i < SCREEN_CAPTURE_COPIES; i++) {
		glDeleteSync(data->fence[i]);
	}
	free(data);
}

/** Copies over the content of src_buffer to tracy. */
void tracy_capture_buffer(struct tracy_data *tracy_data, struct fx_framebuffer *src_buffer) {
#ifdef TRACY_ON_DEMAND
	if (!TracyCIsConnected) {
		return;
	}
#endif

	struct tracy_capture_data *data = tracy_data->tracy_capture_data;
	if (!data) {
		return;
	}

	// Save the currently bound fbos
	GLint draw_fbo_id = 0, read_fbo_id = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo_id);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo_id);

	// From the tracy documentation:
	//	https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf
	while (data->queue.size > 0) {
		int *queue_data = data->queue.data;
		const int fence_i = queue_data[0];
		if (glClientWaitSync(data->fence[fence_i], 0, 0) == GL_TIMEOUT_EXPIRED) {
			break;
		}
		glDeleteSync(data->fence[fence_i]);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, data->pbo[fence_i]);
		void *ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
				// NOTE: Assume 4 bytes per pixel
				SCREEN_CAPTURE_DST_WIDTH * SCREEN_CAPTURE_DST_HEIGHT * 4,
				GL_MAP_READ_BIT);
		___tracy_emit_frame_image(ptr, SCREEN_CAPTURE_DST_WIDTH, SCREEN_CAPTURE_DST_HEIGHT, data->queue.size, false);
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

		// Pop the first fence
		memmove(&queue_data[0], &queue_data[sizeof(int)], data->queue.size - sizeof(int));
		data->queue.size -= sizeof(int);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, src_buffer->fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, data->fbo[data->index]);
	glBlitFramebuffer(0, 0, src_buffer->buffer->width, src_buffer->buffer->height,
			0, 0, SCREEN_CAPTURE_DST_WIDTH, SCREEN_CAPTURE_DST_HEIGHT,
			GL_COLOR_BUFFER_BIT, GL_LINEAR);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, data->fbo[data->index]);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, data->pbo[data->index]);
	glReadPixels(0, 0, SCREEN_CAPTURE_DST_WIDTH, SCREEN_CAPTURE_DST_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	// Restore the bound buffers
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo_id);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo_id);

	data->fence[data->index] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

	// Append the new fence index
	int *index = wl_array_add(&data->queue, sizeof(int));
	*index = data->index;
	data->index = (data->index + 1) % 4;
}

/*
 * GPU Zone
 */

static inline unsigned int get_next_query_index(struct tracy_data *tracy_data) {
	const unsigned int id = tracy_data->q_head;
	tracy_data->q_head = (tracy_data->q_head + 1) % QUERY_COUNT;
	assert(tracy_data->q_head != tracy_data->q_tail);
	return id;
}

void tracy_gpu_zone_begin(struct tracy_data *tracy_data, struct tracy_gpu_zone_context *out_ctx,
		const int line, const char *source, const char *func, const char *name) {
	if (out_ctx == NULL || tracy_data == NULL) {
		return;
	}

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
	const unsigned int query = get_next_query_index(tracy_data);
	tracy_data->renderer->procs.glQueryCounterEXT(tracy_data->queries[query], GL_TIMESTAMP_EXT);

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

void tracy_gpu_zone_end(struct tracy_gpu_zone_context *ctx) {
	if (ctx == NULL || ctx->tracy_data == NULL || !ctx->is_active) {
		return;
	}
	struct tracy_data *tracy_data = ctx->tracy_data;

	// Create the query object
	const unsigned int query = get_next_query_index(tracy_data);
	tracy_data->renderer->procs.glQueryCounterEXT(tracy_data->queries[query], GL_TIMESTAMP_EXT);

	// End the Tracy GPU zone
	const struct ___tracy_gpu_zone_end_data data = {
		.context = tracy_data->context_id,
		.queryId = (uint16_t) query,
	};
	___tracy_emit_gpu_zone_end(data);
}

void tracy_gpu_context_collect(struct tracy_data *tracy_data) {
	if (tracy_data == NULL) {
		// TODO: fix compilation error
		// TracyCMessageL("tracy_data == NULL");
		return;
	}

	TracyCZoneC(ctx, RED4, true);

	if (tracy_data->q_tail == tracy_data->q_head) {
		goto done;
	}

#ifdef TRACY_ON_DEMAND
	if (!TracyCIsConnected) {
		tracy_data->q_head = 0;
		tracy_data->q_tail = 0;
		goto done;
	}
#endif

	while (tracy_data->q_tail != tracy_data->q_head) {
		GLint available;
		tracy_data->renderer->procs.glGetQueryObjectivEXT(
				tracy_data->queries[tracy_data->q_tail],
				GL_QUERY_RESULT_AVAILABLE_EXT, &available);
		if (!available) {
			goto done;
		}

		GLuint64 time;
		tracy_data->renderer->procs.glGetQueryObjectui64vEXT(
				tracy_data->queries[tracy_data->q_tail],
				GL_QUERY_RESULT_EXT, &time);

		const struct ___tracy_gpu_time_data data = {
			.context = tracy_data->context_id,
			.gpuTime = time,
			.queryId = (uint16_t) tracy_data->q_tail,
		};
		___tracy_emit_gpu_time(data);

		tracy_data->q_tail = (tracy_data->q_tail + 1) % QUERY_COUNT;
	}

done:
	TracyCZoneEnd(ctx);
}

void tracy_gpu_context_destroy(struct tracy_data *tracy_data) {
	id_counter--;
	if (tracy_data == NULL) {
		return;
	}

	tracy_capture_destroy(tracy_data->tracy_capture_data);
	tracy_data->tracy_capture_data = NULL;

	tracy_data->renderer->procs.glDeleteQueriesEXT(QUERY_COUNT, tracy_data->queries);
	free(tracy_data);
}

struct tracy_data *tracy_gpu_context_new(struct fx_renderer *renderer) {
	assert(renderer);

	struct tracy_data *tracy_data = calloc(1, sizeof(*tracy_data));
	if (tracy_data == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	tracy_data->renderer = renderer;
	tracy_data->context_id = ++id_counter;
	tracy_data->q_head = 0;
	tracy_data->q_tail = 0;

	tracy_data->tracy_capture_data = tracy_capture_init(tracy_data);

	// Create the query objects
	renderer->procs.glGenQueriesEXT(QUERY_COUNT, tracy_data->queries);

	// Get the current GL time
	GLint64 gl_gpu_time;
	renderer->procs.glGetInteger64vEXT(GL_TIMESTAMP_EXT, &gl_gpu_time);

	// Create the query objects
	GLint bits;
	renderer->procs.glGetQueryivEXT(GL_TIMESTAMP_EXT, GL_QUERY_COUNTER_BITS_EXT, &bits);

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
		 glGetString(GL_RENDERER)) + 1; \
	char ctx_name[len]; \
	snprintf(ctx_name, len, "FX Renderer (GLESV2): %s",
		glGetString(GL_RENDERER)); \
	const struct ___tracy_gpu_context_name_data name_data = {
		.context = tracy_data->context_id,
		.name = ctx_name,
		.len = strlen(ctx_name),
	};
	___tracy_emit_gpu_context_name(name_data);
	return tracy_data;
}
#endif /* ifdef TRACY_ENABLE */
