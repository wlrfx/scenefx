precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;
uniform float radius;

uniform vec2 window_size;
uniform vec2 window_position;
uniform float window_radius;
uniform bool window_round_top_left;
uniform bool window_round_top_right;
uniform bool window_round_bottom_left;
uniform bool window_round_bottom_right;

uniform bool round_top_left;
uniform bool round_top_right;
uniform bool round_bottom_left;
uniform bool round_bottom_right;

float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br);

float hole_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br);

float roundRectSDF(vec2 half_size, vec2 position, float radius);

void main() {
    float corner_alpha = corner_alpha(size, position, radius,
            round_top_left, round_top_right, round_bottom_left, round_bottom_right);
    float window_corner_alpha = hole_alpha(window_size, window_position, window_radius,
            window_round_top_left, window_round_top_right,
            window_round_bottom_left, window_round_bottom_right);

    gl_FragColor = mix(v_color, vec4(0.0), corner_alpha) * window_corner_alpha;
}
