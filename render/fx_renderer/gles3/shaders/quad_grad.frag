#version 300 es

#define LEN %d

precision highp float;

in vec4 v_color;
in vec2 v_texcoord;

uniform vec4 colors[LEN];
uniform vec2 size;
uniform float degree;
uniform vec2 grad_box;
uniform vec2 origin;
uniform bool linear;
uniform bool blend;
uniform int count;

out vec4 fragColor;

vec4 gradient(vec4 colors[LEN], int count, vec2 size, vec2 grad_box, vec2 origin, float degree, bool linear, bool blend);

void main(){
	fragColor = gradient(colors, count, size, grad_box, origin, degree, linear, blend);
}
