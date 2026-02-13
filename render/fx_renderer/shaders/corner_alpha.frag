float get_dist(vec2 q, float radius) {
	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

// Note: Returns 1.0 if outside, 0.0 if inside the bounds.
float corner_alpha(vec2 size, vec2 position, float radius_tl, float radius_tr, float radius_bl, float radius_br, bool inverse) {
	if (radius_tl <= 0.0
			&& radius_tr <= 0.0
			&& radius_bl <= 0.0
			&& radius_br <= 0.0) {
		return inverse ? 1.0 : 0.0;
	}

	vec2 relative_pos = (gl_FragCoord.xy - position);

	if (relative_pos.x < 0.0 || relative_pos.y < 0.0
			|| relative_pos.x > size.x || relative_pos.y > size.y) {
		if (inverse) {
			return 1.0;
		}
		discard;
	}

	bool is_tl = radius_tl > 0.0
		&& relative_pos.x <= radius_tl
		&& relative_pos.y <= radius_tl;
	bool is_tr = radius_tr > 0.0
		&& relative_pos.x >= size.x - radius_tr
		&& relative_pos.y <= radius_tr;
	bool is_bl = radius_bl > 0.0
		&& relative_pos.x <= radius_bl
		&& relative_pos.y >= size.y - radius_bl;
	bool is_br = radius_br > 0.0
		&& relative_pos.x >= size.x - radius_br
		&& relative_pos.y >= size.y - radius_br;
	if (!(is_tl || is_tr || is_bl || is_br)) {
		return 0.0;
	}

	float dist = 0.0;
	if (is_tl) {
		vec2 top_left = abs(relative_pos - size) - size + radius_tl;
		dist = max(dist, get_dist(top_left, radius_tl));
	}
	if (is_tr) {
		vec2 top_right = abs(relative_pos - vec2(0, size.y)) - size + radius_tr;
		dist = max(dist, get_dist(top_right, radius_tr));
	}
	if (is_bl) {
		vec2 bottom_left = abs(relative_pos - vec2(size.x, 0)) - size + radius_bl;
		dist = max(dist, get_dist(bottom_left, radius_bl));
	}
	if (is_br) {
		vec2 bottom_right = abs(relative_pos) - size + radius_br;
		dist = max(dist, get_dist(bottom_right, radius_br));
	}
	return smoothstep(0.0, 1.0, dist);
}
