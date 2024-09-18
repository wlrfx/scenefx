precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform float radius;
uniform vec2 window_position;
uniform vec2 half_window_size;
uniform float half_thickness;

float roundRectSDF() {
	vec2 center = abs(gl_FragCoord.xy - window_position - half_window_size);
	vec2 q = abs(center) - half_window_size - half_thickness + radius;
	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
	float dist = abs(roundRectSDF()) - half_thickness;
	float blend_amount = smoothstep(-1.0, 1.0, dist);
	gl_FragColor = mix(v_color, vec4(0.0), blend_amount);
}

