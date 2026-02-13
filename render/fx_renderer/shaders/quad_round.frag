#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;
uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;

uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_radius_top_left;
uniform float clip_radius_top_right;
uniform float clip_radius_bottom_left;
uniform float clip_radius_bottom_right;

float corner_alpha(vec2 size, vec2 position, float radius_tl, float radius_tr, float radius_bl, float radius_br, bool inverse);

void main() {
	float quad_corner_alpha = corner_alpha(
		size,
		position,
		radius_top_left,
		radius_top_right,
		radius_bottom_left,
		radius_bottom_right,
		false
	);

	// Clipping
	float clip_corner_alpha = corner_alpha(
		clip_size,
		clip_position,
		clip_radius_top_left,
		clip_radius_top_right,
		clip_radius_bottom_left,
		clip_radius_bottom_right,
		true
	);

	gl_FragColor = mix(v_color, vec4(0.0), quad_corner_alpha) * clip_corner_alpha;
}
