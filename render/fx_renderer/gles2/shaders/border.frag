precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform float radius;
uniform vec2 window_position;
uniform vec2 half_window_size;
uniform float half_thickness;

float roundedRectSDF(vec2 center, vec2 size, float radius) {
    return length(max(abs(center) - size + radius, 0.0)) - radius;
}

void main() {
	vec2 center = gl_FragCoord.xy - window_position - half_window_size;
    float dist = roundedRectSDF(center, half_window_size + half_thickness, radius -
	half_thickness);
    dist = abs(dist) - half_thickness;
    float blend_amount = smoothstep(-1.0, 1.0, dist);
    gl_FragColor = mix(v_color, vec4(0.0), blend_amount);
}

