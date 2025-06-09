#version 300 es

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

precision highp float;

in vec2 v_texcoord;

#if SOURCE == SOURCE_TEXTURE_EXTERNAL
uniform samplerExternalOES tex;
#elif SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_RGBX
uniform sampler2D tex;
#endif

uniform float alpha;

uniform vec2 size;
uniform vec2 position;
uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;

uniform bool discard_transparent;

out vec4 fragColor;

vec4 sample_texture() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex, v_texcoord).rgb, 1.0);
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
	fragColor = mix(sample_texture() * alpha, vec4(0.0), corner_alpha);

	if (discard_transparent && fragColor.a == 0.0) {
		discard;
		return;
	}
}
