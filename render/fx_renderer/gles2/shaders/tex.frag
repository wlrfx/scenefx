#define SOURCE_TEXTURE_RGBA 1
#define SOURCE_TEXTURE_RGBX 2
#define SOURCE_TEXTURE_EXTERNAL 3

#if !defined(SOURCE)
#error "Missing shader preamble"
#endif

#if SOURCE == SOURCE_TEXTURE_EXTERNAL
#extension GL_OES_EGL_image_external : require
#endif

precision mediump float;

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

vec4 sample_texture() {
#if SOURCE == SOURCE_TEXTURE_RGBA || SOURCE == SOURCE_TEXTURE_EXTERNAL
	return texture2D(tex, v_texcoord);
#elif SOURCE == SOURCE_TEXTURE_RGBX
	return vec4(texture2D(tex, v_texcoord).rgb, 1.0);
#endif
}

void main() {
	gl_FragColor = sample_texture() * alpha;
	vec2 corner_distance = min(gl_FragCoord.xy - position, size + position - gl_FragCoord.xy);
	if (max(corner_distance.x, corner_distance.y) < radius) {
		float d = radius - distance(corner_distance, vec2(radius));
		float smooth = smoothstep(-1.0, 0.5, d);
		gl_FragColor = mix(vec4(0), gl_FragColor, smooth);
	}
}
