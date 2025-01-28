// TODO: surely I can be optimized
float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br) {
	vec2 relative_pos = (gl_FragCoord.xy - position);

	vec2 top_left = abs(relative_pos - size) - size + radius;
	vec2 top_right = abs(relative_pos - vec2(0, size.y)) - size + radius;
	vec2 bottom_left = abs(relative_pos - vec2(size.x, 0)) - size + radius;
	vec2 bottom_right = abs(relative_pos) - size + radius;

	return max(
		max(
			float(round_tl) * smoothstep(-1.0, 1.0, min(max(top_left.x, top_left.y), 0.0) + length(max(top_left, 0.0)) - radius),
			float(round_tr) * smoothstep(-1.0, 1.0, min(max(top_right.x, top_right.y), 0.0) + length(max(top_right, 0.0)) - radius)
		),
		max(
			float(round_bl) * smoothstep(-1.0, 1.0, min(max(bottom_left.x, bottom_left.y), 0.0) + length(max(bottom_left, 0.0)) - radius),
			float(round_br) * smoothstep(-1.0, 1.0, min(max(bottom_right.x, bottom_right.y), 0.0) + length(max(bottom_right, 0.0)) - radius)
		)
    );
}
