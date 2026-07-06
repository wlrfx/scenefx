#version 450
#extension GL_ARB_shading_language_include : require

layout(location = 0) out vec4 out_color;
layout(push_constant) uniform UBO {
	layout(offset = 80) vec4 color;
	vec2 size;
	vec2 position;
	float radius_top_left;
	float radius_top_right;
	float radius_bottom_left;
	float radius_bottom_right;

	vec2 clip_size;
	vec2 clip_position;
	float clip_radius_top_left;
	float clip_radius_top_right;
	float clip_radius_bottom_left;
	float clip_radius_bottom_right;
} data;

layout (constant_id = 0) const int EFFECTS = 0;

// Matches enum fx_quad_shader_effects
#define EFFECT_ROUND_CORNERS 1
#define EFFECT_CLIPPING 2

#include "corner_alpha.glsl"

void main() {
	out_color = data.color;

	// Corner rounding
	if ((EFFECTS & EFFECT_ROUND_CORNERS) == EFFECT_ROUND_CORNERS) {
		out_color *= corner_alpha(
			data.size - 1.0,
			data.position + 0.5,
			false,
			data.radius_top_left,
			data.radius_top_right,
			data.radius_bottom_left,
			data.radius_bottom_right
		);
	}

	// Clipping
	if ((EFFECTS & EFFECT_CLIPPING) == EFFECT_CLIPPING) {
		out_color *= corner_alpha(
			data.clip_size - 1.0,
			data.clip_position + 0.5,
			true,
			data.clip_radius_top_left,
			data.clip_radius_top_right,
			data.clip_radius_bottom_left,
			data.clip_radius_bottom_right
		);
	}
}
