#ifndef _MATRIX_H
#define _MATRIX_H

#include <wlr/types/wlr_output.h>

/**
 * Writes a 2D orthographic projection matrix to mat of (width, height) with a
 * specified wl_output_transform.
 *
 * Equivalent to glOrtho(0, width, 0, height, 1, -1) with the transform applied.
 */
void matrix_projection(float mat[static 9], int width, int height,
	enum wl_output_transform transform);

#endif
