precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;
uniform float radius;

uniform vec2 window_half_size;
uniform vec2 window_position;
uniform float window_radius;

uniform bool round_top_left;
uniform bool round_top_right;
uniform bool round_bottom_left;
uniform bool round_bottom_right;

float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br);

float roundRectSDF(vec2 half_size, vec2 position, float radius);

void main() {
    float corner_alpha = corner_alpha(size, position, radius,
            round_top_left, round_top_right, round_bottom_left, round_bottom_right);
    float window_alpha = smoothstep(-1.0, 1.0, roundRectSDF(window_half_size, window_position, window_radius + 1.0)); // pull in radius by 1.0 px

    gl_FragColor = mix(v_color, vec4(0.0), corner_alpha) * window_alpha;
}
