#if !defined(SHADER_PREAMBLE)
#error "Missing shader preamble"
#endif

#if MAX_GRADIENT_COLORS > 0
	#define EFFECTS_GRADIENT 1
#else
	#define EFFECTS_GRADIENT 0
#endif

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;

uniform bool effects_rounding;
uniform float radius_top_left;
uniform float radius_top_right;
uniform float radius_bottom_left;
uniform float radius_bottom_right;
uniform float rounding_power;

uniform bool effects_clip;
uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float clip_radius_top_left;
uniform float clip_radius_top_right;
uniform float clip_radius_bottom_left;
uniform float clip_radius_bottom_right;

uniform int fill_type;

uniform vec4 color;

#if EFFECTS_GRADIENT
uniform int gradient_kind;
uniform int gradient_colors_size;
uniform vec4 gradient_colors[MAX_GRADIENT_COLORS];
uniform vec2 gradient_size;
uniform float gradient_angle;
uniform vec2 gradient_box;
uniform vec2 gradient_origin;
uniform bool gradient_blend;

vec4 gradient(int kind, vec4 colors[MAX_GRADIENT_COLORS], int count, vec2 uv,
	vec2 origin, float angle, bool blend);
#endif


float corner_alpha(vec2 size, vec2 position, bool is_cutout, float rounding_power,
		float radius_tl, float radius_tr, float radius_bl, float radius_br);

void main() {
	float alpha = 1.0;

	if(effects_rounding) {
		alpha *= corner_alpha(
			size - 1.0,
			position + 0.5,
			false,
			rounding_power,
			radius_top_left,
			radius_top_right,
			radius_bottom_left,
			radius_bottom_right
		);
	}

	if(effects_clip) {
		// Clipping
		alpha *= corner_alpha(
			clip_size - 1.0,
			clip_position + 0.5,
			true,
			rounding_power,
			clip_radius_top_left,
			clip_radius_top_right,
			clip_radius_bottom_left,
			clip_radius_bottom_right
		);
	}

	vec4 out_color = ERROR_COLOR;
	if(fill_type == FILL_SOLID_COLOR) {
		out_color = color;
	} else if(fill_type == FILL_GRADIENT) {
#if EFFECTS_GRADIENT
		// UVs of the rect may be calculated as
	    //	 vec2 uv = (gl_FragCoord.xy - position) / size;
		// But we instead remap UVs so that they adhere to the sizing of
		// gradient_box+gradient_size.
		vec2 uv = ((gl_FragCoord.xy - position) + gradient_box) / gradient_size;
		vec4 gradient_color = gradient(
			gradient_kind, gradient_colors, gradient_colors_size, uv,
			gradient_origin, gradient_angle, gradient_blend);
		out_color = gradient_color;
#else
		out_color = ERROR_COLOR;
#endif
	}

	gl_FragColor = out_color * alpha;
}
