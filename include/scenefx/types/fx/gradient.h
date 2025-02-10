#ifndef TYPES_FX_GRADIENT_H
#define TYPES_FX_GRADIENT_H

struct gradient {
    float degree;
    struct wlr_box range; // the full area the gradient fit to, for borders use the window size
    float origin[2]; //  center of the gradient, { 0.5, 0.5 } for normal
    bool is_linear; // else is conic
    float should_blend;

    int count;
    float *colors;
};


#endif // TYPES_FX_GRADIENT_H
