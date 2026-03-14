float sd_rounded_box(vec2 position, vec4 center_x, vec4 center_y, vec4 radii) {
    // calculate 'v' (local distance from each corner center)
    vec4 vx = (position.x - center_x) * vec4(1.0, 1.0, -1.0, -1.0);
    vec4 vy = (position.y - center_y) * vec4(1.0, -1.0, 1.0, -1.0);

    // Vectorized SDF logic: Processes all 4 corners at once
    vec4 x_max = max(vx, 0.0);
    vec4 y_max = max(vy, 0.0);
    vec4 dists = sqrt(x_max * x_max + y_max * y_max) + min(max(vx, vy), 0.0) - radii;

    return max(max(dists.x, dists.y), max(dists.z, dists.w));
}

float corner_alpha(vec2 size, vec2 position,
		float radius_tl, float radius_tr, float radius_bl, float radius_br) {
	// TODO: precompute these
	vec4 radii = vec4(radius_tr, radius_br, radius_tl, radius_bl);
	vec4 center_x = (size.x - radii) * vec4(1.0, 1.0, -1.0, -1.0);
	vec4 center_y = (size.y - radii) * vec4(1.0, -1.0, 1.0, -1.0);

	vec2 relative_pos = (gl_FragCoord.xy - position);
    float dist = sd_rounded_box(relative_pos, center_x, center_y, radii);
	return smoothstep(0.0, 1.0, dist);
}

