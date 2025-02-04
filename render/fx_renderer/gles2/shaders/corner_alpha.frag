// https://www.shadertoy.com/view/4llXD7

// b.x = half width
// b.y = half height
// r.x = roundness top-right
// r.y = roundness bottom-right
// r.z = roundness top-left
// r.w = roundness bottom-left
float round_rect_sdf(vec2 p, vec2 b, vec4 r) {
    r.xy = p.x > 0.0 ? r.xy : r.zw;
    r.x  = p.y > 0.0 ? r.x  : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x;
}

float corner_alpha(vec2 size, vec2 position, float radius_tl, float radius_tr, float radius_bl, float radius_br) {
	vec2 relative_pos = (gl_FragCoord.xy - position);
	float dist = round_rect_sdf(relative_pos, size, vec4(radius_tr, radius_br, radius_tl, radius_bl));
	return smoothstep(0.0, 1.0, dist);
}
