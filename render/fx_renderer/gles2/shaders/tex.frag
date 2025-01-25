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

uniform vec2 size;
uniform vec2 position;
uniform float radius;
uniform bool discard_transparent;

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

float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br);

void main() {
	float corner_alpha = corner_alpha(size, position, radius,
			round_top_left, round_top_right, round_bottom_left, round_bottom_right);
	gl_FragColor = mix(sample_texture() * alpha, vec4(0.0), corner_alpha);

	if (discard_transparent && gl_FragColor.a == 0.0) {
		discard;
		return;
	}
}
