#version 300 es

#define HAS_OUTER_COLOR %d



precision highp float;

in vec4 v_color;
in vec2 v_texcoord;

#if HAS_OUTER_COLOR
uniform vec4 outer_color;
#endif

uniform vec2 size;
uniform vec2 position;
uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;

uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_radius_top_left;
uniform float clip_radius_top_right;
uniform float clip_radius_bottom_left;
uniform float clip_radius_bottom_right;

out vec4 fragColor;

float corner_alpha(vec2 size, vec2 position, float round_tl, float round_tr, float round_bl, float round_br);

void main() {
    float quad_corner_alpha = corner_alpha(
        size - 1.0,
        position + 0.5,
        radius_top_left,
        radius_top_right,
        radius_bottom_left,
        radius_bottom_right
    );

    #if !HAS_OUTER_COLOR
    vec4 outer_color = vec4(0.0);
    #endif

    // Clipping
    float clip_corner_alpha = corner_alpha(
        clip_size - 1.0,
        clip_position + 0.5,
        clip_radius_top_left,
        clip_radius_top_right,
        clip_radius_bottom_left,
        clip_radius_bottom_right
    );

    fragColor = mix(v_color, outer_color, quad_corner_alpha) * clip_corner_alpha;
}
