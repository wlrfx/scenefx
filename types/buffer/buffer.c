#include "drm_fourcc.h"
#include "render/pixel_format.h"
#include "types/wlr_buffer.h"

bool buffer_is_opaque(struct wlr_buffer *buffer) {
	void *data;
	uint32_t format;
	size_t stride;
	struct wlr_dmabuf_attributes dmabuf;
	struct wlr_shm_attributes shm;
	if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
		format = dmabuf.format;
	} else if (wlr_buffer_get_shm(buffer, &shm)) {
		format = shm.format;
	} else if (wlr_buffer_begin_data_ptr_access(buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
		bool opaque = false;
		if (buffer->width == 1 && buffer->height == 1 && format == DRM_FORMAT_ARGB8888) {
			// Special case for single-pixel-buffer-v1
			const uint8_t *argb8888 = data; // little-endian byte order
			opaque = argb8888[3] == 0xFF;
		}
		wlr_buffer_end_data_ptr_access(buffer);
		if (opaque) {
			return true;
		}
	} else {
		return false;
	}

	return !pixel_format_has_alpha(format);
}
