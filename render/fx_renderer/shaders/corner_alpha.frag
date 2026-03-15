float corner_alpha(vec2 size, vec2 position,
		float radius_tl, float radius_tr, float radius_bl, float radius_br) {
	vec2 half_size = size * 0.5;
	vec2 relative_pos = gl_FragCoord.xy - position - half_size;

	// TODO: precompute these: function args should be float corner_alpha(position, center_x, center_y, radii)
	//vec4 radii = vec4(radius_tr, radius_br, radius_tl, radius_bl);
	vec4 radii = vec4(radius_tr, radius_br, radius_tl, radius_bl);
	vec4 center_x = (half_size.x - radii) * vec4(1.0, 1.0, -1.0, -1.0);
	vec4 center_y = (half_size.y - radii) * vec4(-1.0, 1.0, -1.0, 1.0);

	// calculate 'v' (local distance from each corner center)
	vec4 vx = (relative_pos.x - center_x) * vec4(1.0, 1.0, -1.0, -1.0);
	vec4 vy = (relative_pos.y - center_y) * vec4(-1.0, 1.0, -1.0, 1.0);

	// Vectorized SDF logic: Process all 4 corners at once
	vec4 x_max = max(vx, 0.0);
	vec4 y_max = max(vy, 0.0);
	vec4 dists = vec4(length(vec2(x_max.x, y_max.x)),
                      length(vec2(x_max.y, y_max.y)),
                      length(vec2(x_max.z, y_max.z)),
                      length(vec2(x_max.w, y_max.w))) + min(max(vx, vy), 0.0) - radii;
	float dist = max(max(dists.x, dists.y), max(dists.z, dists.w));

	return smoothstep(0.0, 1.0, dist);
}

