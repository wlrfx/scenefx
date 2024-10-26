#define SOURCE_TEXTURE_RGBA 1
#define SOURCE_TEXTURE_RGBX 2
#define SOURCE_TEXTURE_EXTERNAL 3

#if !defined(SOURCE)
#error "Missing shader preamble"
#endif

#if SOURCE == SOURCE_TEXTURE_EXTERNAL
#extension GL_OES_EGL_image_external : require
#endif

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec2 v_texcoord;

#if SOURCE == SOURCE_TEXTURE_EXTERNAL
uniform samplerExternalOES tex;
#elif SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_RGBX
uniform sampler2D tex;
#endif

uniform float alpha;

uniform vec2 half_size;
uniform vec2 position;
uniform float radius;
uniform bool discard_transparent;
uniform float dim;
uniform vec4 dim_color;

uniform bool round_top_left;
uniform bool round_top_right;
uniform bool round_bottom_left;
uniform bool round_bottom_right;

vec4 sample_texture() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex, v_texcoord).rgb, 1.0);
#endif
}

float roundRectSDF() {
	vec2 q = abs(gl_FragCoord.xy - position - half_size) - half_size + radius;
	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
	gl_FragColor = mix(sample_texture(), dim_color, dim) * alpha;

	if (discard_transparent && gl_FragColor.a == 0.0) {
		discard;
		return;
	}

	if (round_top_left || round_top_right || round_bottom_left ||
		round_bottom_right) {
		vec2 relative_pos = (gl_FragCoord.xy - position);
		vec2 size = half_size * 2.0;

		// Brachless baby!
		// Selectively round individual corners
		float top = step(relative_pos.y - radius, 0.0);
		float left = step(relative_pos.x - radius, 0.0);
		float right = step(size.x - radius, relative_pos.x);
		float bottom = step(size.y - radius, relative_pos.y);

		float top_left = top * left * float(round_top_left);
		float top_right = top * right * float(round_top_right);
		float bottom_left = bottom * left * float(round_bottom_left);
		float bottom_right = bottom * right * float(round_bottom_right);
		// A value of 1 means that we're allowed to round at this position
		float round_corners = top_left + top_right + bottom_left + bottom_right;

		float alpha = smoothstep(-1.0, 1.0, roundRectSDF());
		gl_FragColor = mix(gl_FragColor, vec4(0.0), alpha * round_corners);
	}
}
