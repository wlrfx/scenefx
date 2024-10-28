#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

vec4 gradient(vec4 colors[LEN], int count, vec2 size, vec2 grad_box, vec2 origin, float degree, bool linear, bool blend){
	float step;

	vec2 normal = (gl_FragCoord.xy - grad_box)/size;
	vec2 uv = normal - origin;

	float rad = radians(degree);

	if (linear) {
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

	if (!blend) {
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
