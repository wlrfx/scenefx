float roundRectSDF(vec2 half_size, vec2 position, float radius) {
	vec2 q = abs(gl_FragCoord.xy - position - half_size) - half_size + radius;
	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}
