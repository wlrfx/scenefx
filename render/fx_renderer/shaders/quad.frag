#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_radius_top_left;
uniform float clip_radius_top_right;
uniform float clip_radius_bottom_left;
uniform float clip_radius_bottom_right;

float corner_alpha(vec2 size, vec2 position, float radius_tl, float radius_tr, float radius_bl, float radius_br);

void main() {
    // Clipping
    float clip_corner_alpha = corner_alpha(
        clip_size - 1.0,
        clip_position + 0.5,
        clip_radius_top_left,
        clip_radius_top_right,
        clip_radius_bottom_left,
        clip_radius_bottom_right
    );

    gl_FragColor = v_color * clip_corner_alpha;
}
