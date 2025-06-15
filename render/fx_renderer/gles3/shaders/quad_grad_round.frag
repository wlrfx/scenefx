#version 300 es

#define LEN %d

precision highp float;

in vec4 v_color;
in vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;

uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;

uniform vec4 colors[LEN];
uniform vec2 grad_size;
uniform float degree;
uniform vec2 grad_box;
uniform vec2 origin;
uniform bool linear;
uniform bool blend;
uniform int count;

out vec4 fragColor;

vec4 gradient(vec4 colors[LEN], int count, vec2 size, vec2 grad_box, vec2 origin, float degree, bool linear, bool blend);

float corner_alpha(vec2 size, vec2 position, float round_tl, float round_tr, float round_bl, float round_br);

// TODO:
void main() {
    float quad_corner_alpha = corner_alpha(
        size - 1.0,
        position + 0.5,
        radius_top_left,
        radius_top_right,
        radius_bottom_left,
        radius_bottom_right
    );
    float rect_alpha = v_color.a * quad_corner_alpha;

    fragColor = mix(vec4(0), gradient(colors, count, size, grad_box, origin, degree, linear, blend), rect_alpha);
}
