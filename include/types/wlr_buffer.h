#ifndef TYPES_WLR_BUFFER
#define TYPES_WLR_BUFFER

#include <wlr/types/wlr_buffer.h>

/**
 * Check whether a buffer is fully opaque.
 *
 * When true is returned, the buffer is guaranteed to be fully opaque, but the
 * reverse is not true: false may be returned in cases where the buffer is fully
 * opaque.
 */
bool buffer_is_opaque(struct wlr_buffer *buffer);

#endif
