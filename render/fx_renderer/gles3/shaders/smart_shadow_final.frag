#version 300 es

precision highp float;

in vec2 v_texcoord;
uniform sampler2D tex;

uniform vec2 size;
uniform vec2 position;
uniform float blur_sigma;
uniform vec4 color;
uniform bool is_horizontal;

out vec4 fragColor;

void main() {
	vec4 tex_sample = texture2D(tex, v_texcoord);
	// TODO: Post processing or just Blit the texture to the main FBO instead?
	fragColor = tex_sample;
}
