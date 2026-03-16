// TODO: remove relative pos - can do this via vertex shader
// we can then do vx and vy precomputed + on the vertex shader

// radii is vec4(radius_tr, radius_br, radius_tl, radius_bl)
float corner_alpha(vec4 radii, vec2 center_pos, vec4 corner_center_x, vec4 corner_center_y) {
	vec2 relative_pos = gl_FragCoord.xy - center_pos;
	// calculate 'v' (local distance from each corner center)
	vec4 vx = (relative_pos.x - corner_center_x) * vec4(1.0, 1.0, -1.0, -1.0);
	vec4 vy = (relative_pos.y - corner_center_y) * vec4(-1.0, 1.0, -1.0, 1.0);

	// Vectorized SDF logic: Process all 4 corners at once
	vec4 x_max = max(vx, 0.0);
	vec4 y_max = max(vy, 0.0);
	vec4 dists = vec4(
		length(vec2(x_max.x, y_max.x)),
		length(vec2(x_max.y, y_max.y)),
		length(vec2(x_max.z, y_max.z)),
		length(vec2(x_max.w, y_max.w))
	) + min(max(vx, vy), 0.0) - radii;
	float dist = max(max(dists.x, dists.y), max(dists.z, dists.w));

	return smoothstep(0.0, 1.0, dist);
}

