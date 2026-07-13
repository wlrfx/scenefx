#define SOURCE %d

#define SOURCE_TEXTURE_RGBA 1
#define SOURCE_TEXTURE_RGBX 2
#define SOURCE_TEXTURE_EXTERNAL 3

#if !defined(SOURCE)
#error "Missing shader preamble"
#endif

#define EFFECTS %d
#define EFFECT_ROUND_CORNERS EFFECTS & 1
#define EFFECT_CLIPPING EFFECTS & 2
#define EFFECT_DISCARD_TRANSPARENT EFFECTS & 3

#if !defined(EFFECTS)
#error "Missing shader effects preamble"
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

#if EFFECT_ROUND_CORNERS
uniform vec2 size;
uniform vec2 position;
uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;
#endif

#if EFFECT_CLIPPING
uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_radius_top_left;
uniform float clip_radius_top_right;
uniform float clip_radius_bottom_left;
uniform float clip_radius_bottom_right;
#endif

vec4 sample_texture() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex, v_texcoord).rgb, 1.0);
#endif
}

#if EFFECT_ROUND_CORNERS || EFFECT_CLIPPING
float corner_alpha(vec2 size, vec2 position, bool is_cutout,
		float radius_tl, float radius_tr, float radius_bl, float radius_br);
#endif

void main() {
	gl_FragColor = sample_texture() * alpha;

#if EFFECT_ROUND_CORNERS
	// Corner rounding
	gl_FragColor *= corner_alpha(
		size - 0.5,
		position + 0.25,
		false,
		radius_top_left,
		radius_top_right,
		radius_bottom_left,
		radius_bottom_right
	);
#endif

#if EFFECT_CLIPPING
	// Clipping
	gl_FragColor *= corner_alpha(
		clip_size - 1.0,
		clip_position + 0.5,
		true,
		clip_radius_top_left,
		clip_radius_top_right,
		clip_radius_bottom_left,
		clip_radius_bottom_right
	);
#endif

#if EFFECT_DISCARD_TRANSPARENT
	if (gl_FragColor.a == 0.0) {
		discard;
	}
#endif
}
