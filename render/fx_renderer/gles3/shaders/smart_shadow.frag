#version 300 es

precision highp float;

in vec2 v_texcoord;

uniform sampler2D tex;

uniform vec2 size;
uniform float blur_sigma;
uniform vec4 color;
uniform vec2 direction;

out vec4 fragColor;

// References:
// - https://lisyarus.github.io/blog/posts/blur-coefficients-generator.html
// - https://stackoverflow.com/a/64845819

// https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
float gaussian(float x) {
	return exp(-(x * x) / (2.0 * blur_sigma * blur_sigma))
		/ (sqrt(2.0 * 3.141592653589793) * blur_sigma);
}

void main() {
	vec4 blur_color = vec4(0.0f);
	int kernelSize = int(ceil(blur_sigma * 3.0f)) * 2 + 1;

	// Run in two passes (one horizontal and one vertical) instead of nested for-loops. O(n*2) instead of O(n^2).
	float weight_sum = 0.0f;
	for (int i = 0; i < kernelSize; ++i) {
		float offset = float(i - kernelSize / 2);
		vec2 tex_offset = vec2(offset / size.x, offset / size.y) * direction;

		float weight = gaussian(offset);
		weight_sum += weight;

		vec4 tex = texture2D(tex, v_texcoord + tex_offset);
		blur_color += tex * weight;
	}

	fragColor = blur_color / weight_sum;

	// TODO: Fix weak colors when opacity is low (compared to the reference CSS
	// drop-shadow implementation)!
	fragColor = vec4(fragColor.a) * color;
}
