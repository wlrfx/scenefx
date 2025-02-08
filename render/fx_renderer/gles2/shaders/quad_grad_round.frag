precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;
uniform float radius;

uniform vec4 colors[LEN];
uniform vec2 grad_size;
uniform float degree;
uniform vec2 grad_box;
uniform vec2 origin;
uniform bool linear;
uniform bool blend;
uniform int count;

uniform bool round_top_left;
uniform bool round_top_right;
uniform bool round_bottom_left;
uniform bool round_bottom_right;

vec4 gradient(vec4 colors[LEN], int count, vec2 size, vec2 grad_box, vec2 origin, float degree, bool linear, bool blend);

float corner_alpha(vec2 size, vec2 position, float round_tl, float round_tr, float round_bl, float round_br);

// TODO:
void main() {
    float quad_corner_alpha = corner_alpha(
        size,
        position,
        float(round_top_left) * radius,
        float(round_top_right) * radius,
        float(round_bottom_left) * radius,
        float(round_bottom_right) * radius
    );
    float rect_alpha = v_color.a * quad_corner_alpha;

    gl_FragColor = mix(vec4(0), gradient(colors, count, size, grad_box, origin, degree, linear, blend), rect_alpha);
}
