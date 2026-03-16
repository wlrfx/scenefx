#define SOURCE %d
#define EFFECTS %d

#define SOURCE_TEXTURE_RGBA 1
#define SOURCE_TEXTURE_RGBX 2
#define SOURCE_TEXTURE_EXTERNAL 3

#if !defined(SOURCE) || !defined(EFFECTS)
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

#if EFFECTS
uniform vec2 center_pos;
uniform vec4 corner_center_x;
uniform vec4 corner_center_y;
uniform vec4 radii;

uniform vec2 clip_center_pos;
uniform vec4 clip_corner_center_x;
uniform vec4 clip_corner_center_y;
uniform vec4 clip_radii;
#endif

uniform bool discard_transparent;

vec4 sample_texture() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex, v_texcoord).rgb, 1.0);
#endif
}

#if EFFECTS
float corner_alpha(vec4 radii, vec2 center_pos, vec4 corner_center_x, vec4 corner_center_y);
#endif

void main() {
#if EFFECTS
	float quad_corner_alpha = corner_alpha(
		radii,
		center_pos,
		corner_center_x,
		corner_center_y
	);

    // Clipping
	float clip_corner_alpha = corner_alpha(
		clip_radii,
		clip_center_pos,
		clip_corner_center_x,
		clip_corner_center_y
	);

	gl_FragColor = mix(sample_texture() * alpha, vec4(0.0), quad_corner_alpha) * clip_corner_alpha;
#else
	gl_FragColor = sample_texture() * alpha;
#endif

	if (discard_transparent && gl_FragColor.a == 0.0) {
		discard;
	}
}
