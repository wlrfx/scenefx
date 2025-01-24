precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;
uniform float radius;

uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_corner_radius;
uniform bool clip_round_top_left;
uniform bool clip_round_top_right;
uniform bool clip_round_bottom_left;
uniform bool clip_round_bottom_right;

uniform bool round_top_left;
uniform bool round_top_right;
uniform bool round_bottom_left;
uniform bool round_bottom_right;

float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br);

void main() {
    float quad_corner_alpha = corner_alpha(size, position,
            radius, round_top_left, round_top_right, round_bottom_left, round_bottom_right);

    // Clipping
    float clip_corner_alpha = corner_alpha(clip_size, clip_position, clip_corner_radius,
            clip_round_top_left, clip_round_top_right,
            clip_round_bottom_left, clip_round_bottom_right);
    // Make sure that there are corners to round, sets the window alpha to 1.0
    // if window CORNER_LOCATION_NONE or window radius is 0.
    float base_case = float(clip_round_top_left) + float(clip_round_top_right)
            + float(clip_round_bottom_left) + float(clip_round_bottom_right);
    base_case *= step(1.0, clip_corner_radius); // Corner radius is 0
    clip_corner_alpha = max(clip_corner_alpha, 1.0 - step(1.0, base_case));

    gl_FragColor = mix(v_color, vec4(0.0), quad_corner_alpha) * clip_corner_alpha;
}
