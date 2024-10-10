precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform bool is_top_left;
uniform bool is_top_right;
uniform bool is_bottom_left;
uniform bool is_bottom_right;

uniform vec2 position;
uniform float radius;
uniform vec2 half_size;
uniform float half_thickness;

uniform vec4 colors[LEN];
uniform vec2 size;
uniform float degree;
uniform vec2 grad_box;
uniform vec2 origin;
uniform bool linear;
uniform bool blend;
uniform int count;

vec4 gradient(){
	float step;

	vec2 normal = (gl_FragCoord.xy - grad_box)/size;
	vec2 uv = normal - origin;

	float rad = radians(degree);

	if(linear){
		float angle = rad + atan(uv.x, uv.y);

		float len = length(uv);
		uv = vec2(cos(angle) * len, sin(angle) * len) + origin;
		step = uv.x;
	} else {
		vec2 uv = normal - origin;
		uv = vec2(uv.x * cos(rad) - uv.y * sin(rad),
				uv.x * sin(rad) + uv.y * cos(rad));

		uv = vec2(-atan(uv.y, uv.x)/3.14159265 * 0.5 + 0.5, 0.0);
		step = uv.x;
	}

	if(!blend){
		float smooth = 1.0/float(count);
		int ind = int(step/smooth);

		return colors[ind];
	}

	float smooth = 1.0/float(count - 1);
    int ind = int(step/smooth);
    float at = float(ind)*smooth;

    vec4 color = colors[ind];
    if(ind > 0) color = mix(colors[ind - 1], color, smoothstep(at - smooth, at, step));
    if(ind <= count - 1) color = mix(color, colors[ind + 1], smoothstep(at, at + smooth, step));

	return color;
}

float roundedBoxSDF(vec2 center, vec2 size, float radius) {
    return length(max(abs(center) - size + radius, 0.0)) - radius;
}

void main() {
    vec2 center = gl_FragCoord.xy - position - half_size;
    float distance = roundedBoxSDF(center, half_size - half_thickness, radius + half_thickness);
    float smoothedAlphaOuter = 1.0 - smoothstep(-1.0, 1.0, distance - half_thickness);
    // Create an inner circle that isn't as anti-aliased as the outer ring
    float smoothedAlphaInner = 1.0 - smoothstep(-1.0, 0.5, distance + half_thickness);

    if (is_top_left && (center.y > 0.0 || center.x > 0.0)) {
        discard;
    } else if (is_top_right && (center.y > 0.0 || center.x < 0.0)) {
        discard;
    } else if (is_bottom_left && (center.y < 0.0 || center.x > 0.0)) {
        discard;
    } else if (is_bottom_right && (center.y < 0.0 || center.x < 0.0)) {
        discard;
    }

    gl_FragColor = mix(vec4(0), gradient(), smoothedAlphaOuter - smoothedAlphaInner);
}
