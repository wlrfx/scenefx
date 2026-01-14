#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform sampler2D tex;

uniform vec2 size;
uniform float blur_sigma;
uniform vec4 color;

void main() {
	vec4 tex_sample = texture2D(tex, v_texcoord);
	// Sample the textures alpha mask and mix it with the shadow color
	gl_FragColor = vec4(color.rgb * tex_sample.a, tex_sample.a * color.a);
}
