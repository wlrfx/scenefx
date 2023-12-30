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
		wlr_buffer_end_data_ptr_access(buffer);
	} else {
		return false;
	}

	const struct wlr_pixel_format_info *format_info =
		drm_get_pixel_format_info(format);
	if (format_info == NULL) {
		return false;
	}

	return !format_info->has_alpha;
}
