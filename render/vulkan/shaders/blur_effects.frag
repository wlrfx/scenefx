#version 450

// Brightness/contrast/saturation/noise applied after the blur chain.
// Port of the GLES2 blur_effects.frag.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform UBO {
	layout(offset = 80) float brightness;
	float contrast;
	float saturation;
	float noise;
} data;

mat4 brightness_matrix() {
	float b = data.brightness - 1.0;
	return mat4(1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				b, b, b, 1);
}

mat4 contrast_matrix() {
	float t = (1.0 - data.contrast) / 2.0;
	return mat4(data.contrast, 0, 0, 0,
				0, data.contrast, 0, 0,
				0, 0, data.contrast, 0,
				t, t, t, 1);
}

mat4 saturation_matrix() {
	vec3 luminance = vec3(0.3086, 0.6094, 0.0820) * (1.0 - data.saturation);
	vec3 red = vec3(luminance.x);
	red.x += data.saturation;
	vec3 green = vec3(luminance.y);
	green.y += data.saturation;
	vec3 blue = vec3(luminance.z);
	blue.z += data.saturation;
	return mat4(red, 0,
				green, 0,
				blue, 0,
				0, 0, 0, 1);
}

float noise_amount(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 1689.1984);
	p3 += dot(p3, p3.yzx + 33.33);
	float hash = fract((p3.x + p3.y) * p3.z);
	return (mod(hash, 1.0) - 0.5) * data.noise;
}

void main() {
	vec4 color = texture(tex, uv);
	// Do *not* transpose the combined matrix when multiplying
	color = brightness_matrix() * contrast_matrix() * saturation_matrix() * color;
	color.xyz += noise_amount(uv);
	out_color = color;
}
