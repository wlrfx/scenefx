#version 300 es

precision highp float;

in vec2 v_texcoord;
uniform sampler2D tex;

uniform vec2 size;
uniform float blur_sigma;
uniform vec4 color;
uniform bool is_horizontal;

out vec4 fragColor;

void main() {
	vec4 tex_sample = texture2D(tex, v_texcoord);
	// Sample the textures alpha mask and mix it with the shadow color
	fragColor = vec4(color.rgb * tex_sample.a, tex_sample.a * color.a);
}
