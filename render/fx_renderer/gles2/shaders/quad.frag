#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_corner_radius;
uniform bool clip_round_top_left;
uniform bool clip_round_top_right;
uniform bool clip_round_bottom_left;
uniform bool clip_round_bottom_right;

float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br);

void main() {
    // Clipping
    float clip_corner_alpha = corner_alpha(clip_size - 1.0, clip_position + 0.5, clip_corner_radius,
            clip_round_top_left, clip_round_top_right,
            clip_round_bottom_left, clip_round_bottom_right);

    float base_case = float(clip_round_top_left) + float(clip_round_top_right)
            + float(clip_round_bottom_left) + float(clip_round_bottom_right);
    base_case *= step(1.0, clip_corner_radius); // Corner radius is 0
    clip_corner_alpha = max(clip_corner_alpha, 1.0 - step(1.0, base_case));

    gl_FragColor = v_color * clip_corner_alpha;
}
