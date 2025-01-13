float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br) {
	vec2 relative_pos = (gl_FragCoord.xy - position);

	vec2 top_left = float(round_tl) * abs(relative_pos - size) - size + radius;
	vec2 top_right = float(round_tr) * abs(relative_pos - vec2(0, size.y)) - size + radius;
	vec2 bottom_left = float(round_br) * abs(relative_pos - vec2(size.x, 0)) - size + radius;
	vec2 bottom_right = float(round_bl) * abs(relative_pos) - size + radius;

	vec2 q = max(max(top_left, top_right), max(bottom_left, bottom_right));

	return smoothstep(-1.0, 1.0, min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius);
}
