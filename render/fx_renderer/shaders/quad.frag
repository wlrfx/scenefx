#version 320 es

#define EFFECTS %d

#if !defined(EFFECTS)
#error "Missing shader preamble"
#endif

precision highp float;

in vec4 v_color;
in vec2 v_texcoord;

#if EFFECTS
uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_radius_top_left;
uniform float clip_radius_top_right;
uniform float clip_radius_bottom_left;
uniform float clip_radius_bottom_right;
#endif

out vec4 fragColor;

float corner_alpha(vec2 size, vec2 position, bool is_cutout,
		float radius_tl, float radius_tr, float radius_bl, float radius_br);

void main() {
#if EFFECTS
	// Clipping
	float clip_corner_alpha = corner_alpha(
		clip_size - 1.0,
		clip_position + 0.5,
		true,
		clip_radius_top_left,
		clip_radius_top_right,
		clip_radius_bottom_left,
		clip_radius_bottom_right
	);

	fragColor = v_color * clip_corner_alpha;
#else
	fragColor = v_color;
#endif
}
