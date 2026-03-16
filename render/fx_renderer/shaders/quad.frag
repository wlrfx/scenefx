#define EFFECTS %d

#if !defined(EFFECTS)
#error "Missing shader preamble"
#endif

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

#if EFFECTS
uniform vec2 clip_center_pos;
uniform vec4 clip_corner_center_x;
uniform vec4 clip_corner_center_y;
uniform vec4 clip_radii;
#endif

float corner_alpha(vec4 radii, vec2 center_pos, vec4 corner_center_x, vec4 corner_center_y);

void main() {
#if EFFECTS
	// Clipping
	float clip_corner_alpha = corner_alpha(
		clip_radii,
		clip_center_pos,
		clip_corner_center_x,
		clip_corner_center_y
	);

	gl_FragColor = v_color * clip_corner_alpha;
#else
	gl_FragColor = v_color;
#endif
}
