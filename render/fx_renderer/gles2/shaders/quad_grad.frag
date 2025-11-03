#define LEN %d

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec4 colors[LEN];
uniform vec2 size;
uniform float degree;
uniform vec2 grad_box;
uniform vec2 origin;
uniform bool is_linear;
uniform bool should_blend;
uniform int count;

vec4 gradient(vec4 colors[LEN], int count, vec2 size, vec2 grad_box,
        vec2 origin, float degree, bool is_linear, bool should_blend);

void main(){
    gl_FragColor = gradient(colors, count, size, grad_box,
            origin, degree, is_linear, should_blend);
}
