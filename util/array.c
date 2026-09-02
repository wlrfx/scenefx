#include "util/array.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

void array_remove_at(struct wl_array *arr, size_t offset, size_t size) {
	// offset + size can overflow, and callers can be built without asserts
	// (NDEBUG), so this needs to be a real check rather than assert-only:
	// letting a bad range through turns the memmove length below into a
	// huge underflowed value.
	if (offset > arr->size || size > arr->size - offset) {
		assert(0 && "array_remove_at: out-of-bounds range");
		return;
	}

	char *data = arr->data;
	memmove(&data[offset], &data[offset + size], arr->size - offset - size);
	arr->size -= size;
}

bool array_realloc(struct wl_array *arr, size_t size) {
	// If the size is less than 1/4th of the allocation size, we shrink it.
	// 1/4th is picked to provide hysteresis, without which an array with size
	// arr->alloc would constantly reallocate if an element is added and then
	// removed continuously.
	size_t alloc;
	if (arr->alloc > 0 && size > arr->alloc / 4) {
		alloc = arr->alloc;
	} else {
		alloc = 16;
	}

	// Checked doubling: bail before a doubling that would overflow alloc
	// instead of wrapping it to a too-small value and under-allocating
	// below.
	while (alloc < size) {
		if (alloc > SIZE_MAX / 2) {
			return false;
		}
		alloc *= 2;
	}

	if (alloc == arr->alloc) {
		return true;
	}

	void *data = realloc(arr->data, alloc);
	if (data == NULL) {
		return false;
	}
	arr->data = data;
	arr->alloc = alloc;
	return true;
}
