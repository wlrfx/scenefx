#define SOURCE_QUAD_ROUND 1
#define SOURCE_QUAD_ROUND_TOP_LEFT 2
#define SOURCE_QUAD_ROUND_TOP_RIGHT 3
#define SOURCE_QUAD_ROUND_BOTTOM_RIGHT 4
#define SOURCE_QUAD_ROUND_BOTTOM_LEFT 5

#if !defined(SOURCE)
#error "Missing shader preamble"
#endif

precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 size;
uniform vec2 position;
uniform float radius;

uniform vec4 colors[LEN];
uniform vec2 grad_size;
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
		uv *= vec2(1.0)/vec2(abs(cos(rad)) + abs(sin(rad)));

		vec2 rotated = vec2(uv.x * cos(rad) - uv.y * sin(rad) + origin.x,
						uv.x * sin(rad) + uv.y * cos(rad) + origin.y);

		step = rotated.x;
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

vec2 getCornerDist() {
#if SOURCE == SOURCE_QUAD_ROUND
    vec2 half_size = size * 0.5;
    return abs(gl_FragCoord.xy - position - half_size) - half_size + radius;
#elif SOURCE == SOURCE_QUAD_ROUND_TOP_LEFT
    return abs(gl_FragCoord.xy - position - size) - size + radius;
#elif SOURCE == SOURCE_QUAD_ROUND_TOP_RIGHT
    return abs(gl_FragCoord.xy - position - vec2(0, size.y)) - size + radius;
#elif SOURCE == SOURCE_QUAD_ROUND_BOTTOM_RIGHT
    return abs(gl_FragCoord.xy - position) - size + radius;
#elif SOURCE == SOURCE_QUAD_ROUND_BOTTOM_LEFT
    return abs(gl_FragCoord.xy - position - vec2(size.x, 0)) - size + radius;
#endif
}

void main() {
    vec2 q = getCornerDist();
    float dist = min(max(q.x,q.y), 0.0) + length(max(q, 0.0)) - radius;
    float smoothedAlpha = 1.0 - smoothstep(-1.0, 0.5, dist);
    gl_FragColor = mix(vec4(0), gradient(), smoothedAlpha);
}
