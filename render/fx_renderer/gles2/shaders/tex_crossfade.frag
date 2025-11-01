#define SOURCE %d

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
uniform samplerExternalOES tex_prev;
uniform samplerExternalOES tex_next;
#elif SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_RGBX
uniform sampler2D tex_prev;
uniform sampler2D tex_next;
#endif

uniform float progress;
uniform float alpha;

uniform vec2 size;
uniform vec2 position;
uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;

uniform bool discard_transparent;

vec4 sample_texture_prev() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex_prev, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex_prev, v_texcoord).rgb, 1.0);
#endif
}

vec4 sample_texture_next() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex_next, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex_next, v_texcoord).rgb, 1.0);
#endif
}

float corner_alpha(vec2 size, vec2 position, float round_tl, float round_tr, float round_bl, float round_br);

void main() {
    float corner_alpha = corner_alpha(
        size - 0.5,
        position + 0.25,
        radius_top_left,
        radius_top_right,
        radius_bottom_left,
        radius_bottom_right
    );
	vec4 color = mix(sample_texture_prev(), sample_texture_next(), progress);
	gl_FragColor = mix(color * alpha, vec4(0.0), corner_alpha);
}
