#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 center_pos;
uniform vec4 corner_center_x;
uniform vec4 corner_center_y;
uniform vec4 radii;

uniform vec2 clip_center_pos;
uniform vec4 clip_corner_center_x;
uniform vec4 clip_corner_center_y;
uniform vec4 clip_radii;

float corner_alpha(vec4 radii, vec2 center_pos, vec4 corner_center_x, vec4 corner_center_y);

void main() {
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

	gl_FragColor = mix(v_color, vec4(0.0), quad_corner_alpha) * clip_corner_alpha;
}
