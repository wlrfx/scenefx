/// norm
///
/// Calculate the p-norm of a vector. The vector components must be
/// non-negative.
///
float norm(vec2 v, float p)
{
	return pow(pow(v.x, p) + pow(v.y, p), 1.0 / p);
}

float calculate_distance(vec2 p, float radius, float rounding_power)
{
	float inner = min(max(p.x, p.y), 0.0);
	vec2 outer = max(p, 0.0);
	return inner + norm(outer, rounding_power) - radius;
}

// Note: Returns 0.0 if outside, 1.0 if inside the bounds. The is_cutout parameter
// reverses this, 0.0 inside cutout and 1.0 for outside cutout.
float corner_alpha(vec2 size, vec2 position, bool is_cutout, float rounding_power,
		float radius_tl, float radius_tr, float radius_bl, float radius_br) {
	if (radius_tl <= 0.0
			&& radius_tr <= 0.0
			&& radius_bl <= 0.0
			&& radius_br <= 0.0) {
		return 1.0;
	}

	vec2 relative_pos = (gl_FragCoord.xy - position);

	if (relative_pos.x < 0.0 || relative_pos.y < 0.0
			|| relative_pos.x > size.x || relative_pos.y > size.y) {
		if (is_cutout) {
			return 1.0;
		}
		discard;
	}

	bool is_top_left = radius_tl > 0.0
		&& relative_pos.x <= radius_tl
		&& relative_pos.y <= radius_tl;
	bool is_top_right = radius_tr > 0.0
		&& relative_pos.x >= size.x - radius_tr
		&& relative_pos.y <= radius_tr;
	bool is_bottom_left = radius_bl > 0.0
		&& relative_pos.x <= radius_bl
		&& relative_pos.y >= size.y - radius_bl;
	bool is_bottom_right = radius_br > 0.0
		&& relative_pos.x >= size.x - radius_br
		&& relative_pos.y >= size.y - radius_br;
	if (!is_top_left && !is_top_right && !is_bottom_left && !is_bottom_right) {
		if (is_cutout) {
			discard;
		}
		return 1.0;
	}

	vec2 top_left = abs(relative_pos - size) - size + radius_tl;
	vec2 top_right = abs(relative_pos - vec2(0, size.y)) - size + radius_tr;
	vec2 bottom_left = abs(relative_pos - vec2(size.x, 0)) - size + radius_bl;
	vec2 bottom_right = abs(relative_pos) - size + radius_br;
	float dist = max(
		max(calculate_distance(top_left, radius_tl, rounding_power),
			calculate_distance(top_right, radius_tr, rounding_power)),
		max(calculate_distance(bottom_left, radius_bl, rounding_power),
			calculate_distance(bottom_right, radius_br, rounding_power))
	);
	float result = smoothstep(0.0, 1.0, dist);
	return is_cutout ? result : 1.0 - result;
}

float corner_alpha(vec2 size, vec2 position, bool is_cutout, 
		float radius_tl, float radius_tr, float radius_bl, float radius_br) {
	return corner_alpha(size, position, is_cutout, 2.0, radius_tl, radius_tr,
			radius_bl, radius_br);
}
