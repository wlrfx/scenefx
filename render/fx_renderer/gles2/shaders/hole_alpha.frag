float corner_alpha(vec2 size, vec2 position, float radius,
			bool round_tl, bool round_tr, bool round_bl, bool round_br);

float hole_alpha(vec2 size, vec2 position, float radius,
			bool round_tl, bool round_tr, bool round_bl, bool round_br) {
	float corner_alpha = corner_alpha(size, position,
			radius, round_tl, round_tr, round_bl, round_br);

	// Make sure that there are corners to round, sets the window alpha to 1.0
	// if window CORNER_LOCATION_NONE or window radius is 0.
	float base_case = float(round_tl) + float(round_tr) + float(round_bl) + float(round_br);
	base_case *= step(1.0, radius); // Corner radius is 0
	corner_alpha = max(corner_alpha, 1.0 - step(1.0, base_case));

	return corner_alpha;
}
