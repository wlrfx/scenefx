#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;
uniform sampler2D tex;

uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float noise;

mat4 brightnessMatrix() {
	float b = brightness - 1.0;
	return mat4(1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				b, b, b, 1);
}

mat4 contrastMatrix() {
	float t = (1.0 - contrast) / 2.0;
	return mat4(contrast, 0, 0, 0,
				0, contrast, 0, 0,
				0, 0, contrast, 0,
				t, t, t, 1);
}

mat4 saturationMatrix() {
	vec3 luminance = vec3(0.3086, 0.6094, 0.0820) * (1.0 - saturation);
	vec3 red = vec3(luminance.x);
	red.x += saturation;
	vec3 green = vec3(luminance.y);
	green.y += saturation;
	vec3 blue = vec3(luminance.z);
	blue.z += saturation;
	return mat4(red, 0,
				green, 0,
				blue, 0,
				0, 0, 0, 1);
}

float noiseAmount(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 1689.1984);
	p3 += dot(p3, p3.yzx + 33.33);
	float hash = fract((p3.x + p3.y) * p3.z);
	return (mod(hash, 1.0) - 0.5) * noise;
};

void main() {
	vec4 color = texture2D(tex, v_texcoord);
	// Do *not* transpose the combined matrix when multiplying
	color = brightnessMatrix() * contrastMatrix() * saturationMatrix() * color;
	color.xyz += noiseAmount(v_texcoord);
	gl_FragColor = color;
}
