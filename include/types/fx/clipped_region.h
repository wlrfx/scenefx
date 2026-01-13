#ifndef TYPES_FX_CLIPPED_REGION_H
#define TYPES_FX_CLIPPED_REGION_H
#include <wlr/util/box.h>
#include <scenefx/types/fx/clipped_region.h>

struct fx_corner_fradii {
    float top_left;
    float top_right;
    float bottom_right;
    float bottom_left;
};


bool fx_corner_fradii_is_empty(const struct fx_corner_fradii* corners);

struct fx_corner_fradii fx_corner_radii_scale(struct fx_corner_radii, float scale);

struct clipped_fregion {
    struct wlr_box area;
    struct fx_corner_fradii corners;
};

#endif