#include <math.h>
#include "util/fx_helpers.h"

int calc_blur_size(int num_passes, int radius) {
	return pow(2, num_passes + 1) * radius;
}
