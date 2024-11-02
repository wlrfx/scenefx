float roundRectSDF(vec2 half_size, vec2 position, float radius);

float corner_alpha(vec2 size, vec2 position, float radius,
            bool round_tl, bool round_tr, bool round_bl, bool round_br) {
	vec2 relative_pos = (gl_FragCoord.xy - position);

	// Brachless baby!
	// Selectively round individual corners
	float top = step(relative_pos.y - radius, 0.0);
	float left = step(relative_pos.x - radius, 0.0);
	float right = step(size.x - radius, relative_pos.x);
	float bottom = step(size.y - radius, relative_pos.y);

	float top_left = top * left * float(round_tl);
	float top_right = top * right * float(round_tr);
	float bottom_left = bottom * left * float(round_bl);
	float bottom_right = bottom * right * float(round_br);
	// A value of 1 means that we're allowed to round at this position
	float round_corners = top_left + top_right + bottom_left + bottom_right;

	float alpha = smoothstep(-1.0, 1.0, roundRectSDF(size * 0.5, position, radius));
	return alpha * round_corners;
}
