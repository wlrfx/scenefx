#if EFFECTS_GRADIENT

vec4 compute_gradient_color_from_blend_factor(
	vec4 colors[MAX_GRADIENT_COLORS], int count, float factor, bool blend)
{
	if(!blend) {
		int inx = clamp(int(float(count) * factor), 0, count - 1);
		return colors[inx];
	}

	float smooth_inx = clamp(factor * float(count - 1), 0.0, float(count - 1));
    int inx = clamp(int(smooth_inx), 0, count - 1);
	if(inx == count - 1) {
		return colors[count - 1];
	}
	float blend_factor = clamp(smooth_inx - floor(smooth_inx), 0.0, 1.0);
	vec4 color = mix(colors[inx], colors[inx + 1], blend_factor);
	return color;
}

vec4 compute_gradient_linear(vec4 colors[MAX_GRADIENT_COLORS], int count, 
		vec2 uv, float angle, bool blend)
{
	if(count <= 1) {
		return colors[0];
	}

	// For reference, see CSS linear-gradient.
	//
	// The math is quite simple. We construct the unit vector n denoting the
	// axis of the gradient from angle. We then project all four corners
	// relative to the midpoint of the square onto n forming a set 
	// S = { A, -A, B, -B } where
	//   A = 0.5 * sin(angle) + 0.5 * cos(angle)
	//   B = 0.5 * sin(angle) - 0.5 * cos(angle)
	// This works because of the symmetry about the axis.
	//
	// The gradient runs between the farthest endpoints in S, thus to find the
	// distance between them we must find the maximum and minimum of the set.
	// For our purposes, the minimum and maximum happen to yield identical
	// results since the sign is irrelevant for length - if B is farthest away,
	// then -B is farthest away in the opposite direction. Thus we effectively
	// are looking for 
	//   2 * max(|A|, |B|) = 2 * max(|s + c|, |s - c|)
	// where s is the sine term and c is the cosine term.
	//
	// There is a closed form for this expression:
	//   max(|s + c|, |s - c|) = |s| + |c|
	// It may be proved using the triangle inequality, but the formal rambling
	// is too long to be included here.
	//
	// Once we have the length, it is trivial to derive the starting endpoint
	// and calculate the distance of projections of fragments onto n from it.

	vec2 n = vec2(-sin(angle), cos(angle));
	float axis_length = abs(n.x) + abs(n.y);	
	vec2 endpoint = vec2(0.5, 0.5) + n * 0.5 * axis_length;	
	float factor = dot(uv - vec2(0.5, 0.5), n) / axis_length + 0.5;
	// We want to reverse the colors to match CSS's order.
	factor = clamp(1.0 - factor, 0.0, 1.0);
	return compute_gradient_color_from_blend_factor(colors, count, factor, blend);
}

vec4 compute_gradient_radial(vec4 colors[MAX_GRADIENT_COLORS], int count,
							 vec2 uv, vec2 origin, bool blend)
{
	const float PI = 3.14159265;

	if(count <= 1) {
		return colors[0];
	}

	// This implements the CSS radial-gradient with `ellipsis` shape and
	// `farthest-corner` size.

	vec2 farthest_corner = vec2(
			origin.x < 0.5 ? 1.0 : 0.0,
			origin.y < 0.5 ? 1.0 : 0.0);
	float factor = distance(uv, origin) / distance(farthest_corner, origin);
	return compute_gradient_color_from_blend_factor(colors, count, factor, blend);
}

vec4 compute_gradient_conic(vec4 colors[MAX_GRADIENT_COLORS], int count, vec2 uv, vec2 origin,
							float degree, bool blend)
{
	const float PI = 3.14159265;

	if(count <= 1) {
		return colors[0];
	}

	// Make UV coordinates relative to the origin.
	uv = uv - origin;

	// We mimic the behaviour of CSS conic-gradient here, thus the gradient must
	// rotate clockwise and the 0deg angle must be along the vertical axis.
	//
	// To rotate the output clockwise, we must rotate the coordinates
	// counterclockwise and subtract degrees (somewhat counterintuitive).

	// Apply rotation by 90deg to align the 0deg angle with the vertical axis.
	uv = vec2(-uv.y, uv.x);
	float angle;
    if (uv.x == 0.0 && uv.y == 0.0) {
		// atan(y, x) is undefined at (0, 0). Making the angle 0 in that case is
		// fine.
        angle = 0.0;
    } else {
        angle = atan(uv.y, uv.x);
    }
	angle -= degree;
	// Remap from [-pi, pi] to [0, 1].
	float factor = mod(angle / PI * 0.5, 1.0);
	return compute_gradient_color_from_blend_factor(colors, count, factor, blend);
}

vec4 gradient(int kind, vec4 colors[MAX_GRADIENT_COLORS], int count, vec2 uv,
	vec2 origin, float angle, bool blend)
{
	angle = radians(angle);
	if(kind == GRADIENT_LINEAR) {
		return compute_gradient_linear(
				colors, count, uv, angle, blend);
	} else if(kind == GRADIENT_RADIAL) {
		return compute_gradient_radial(colors, count, uv, origin, blend);
	} else if(kind == GRADIENT_CONIC) {
		return compute_gradient_conic(
				colors, count, uv, origin, angle, blend);
	} else {
		return ERROR_COLOR;
	}
}

#endif
