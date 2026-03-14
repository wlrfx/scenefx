float corner_alpha(vec2 size, vec2 position,
		float radius_tl, float radius_tr, float radius_bl, float radius_br) {
	// TODO: precompute these: function args should be float corner_alpha(position, center_x, center_y, radii)
	vec4 radii = vec4(radius_tr, radius_br, radius_tl, radius_bl);
	vec4 center_x = (size.x - radii) * vec4(1.0, 1.0, -1.0, -1.0);
	vec4 center_y = (size.y - radii) * vec4(1.0, -1.0, 1.0, -1.0);

	vec2 relative_pos = (gl_FragCoord.xy - position);

	// calculate 'v' (local distance from each corner center)
	vec4 vx = (relative_pos.x - center_x) * vec4(1.0, 1.0, -1.0, -1.0);
	vec4 vy = (relative_pos.y - center_y) * vec4(1.0, -1.0, 1.0, -1.0);

	// Vectorized SDF logic: Processes all 4 corners at once
	vec4 x_max = max(vx, 0.0);
	vec4 y_max = max(vy, 0.0);
	vec4 dists = sqrt(x_max * x_max + y_max * y_max) + min(max(vx, vy), 0.0) - radii;
	float dist = max(max(dists.x, dists.y), max(dists.z, dists.w));

	return smoothstep(0.0, 1.0, dist);
}

