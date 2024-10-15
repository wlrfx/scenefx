precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform float radius;
uniform vec2 window_position;
uniform vec2 half_window_size;
uniform float thickness;

float roundRectSDF(vec2 half_size, vec2 position, float radius) {
    vec2 q = abs(gl_FragCoord.xy - position - half_size) - half_size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
	float border_alpha = smoothstep(-1.0, 1.0, roundRectSDF(half_window_size + thickness, window_position - thickness, radius));
	float window_alpha = smoothstep(-1.0, 1.0, roundRectSDF(half_window_size, window_position, radius + 0.5)); // pull in radius by 0.5 px

	gl_FragColor = mix(v_color, vec4(0.0), border_alpha) * (1.0 - mix(v_color, vec4(0.0), window_alpha).a);
}
