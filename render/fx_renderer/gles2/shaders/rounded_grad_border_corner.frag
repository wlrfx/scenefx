precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform bool is_top_left;
uniform bool is_top_right;
uniform bool is_bottom_left;
uniform bool is_bottom_right;

uniform vec2 position;
uniform float radius;
uniform vec2 half_size;
uniform float half_thickness;

uniform vec4 colors[LEN];
uniform vec2 size;
uniform float degree;
uniform vec2 grad_box;
uniform vec2 origin;
uniform bool linear;
uniform bool blend;
uniform int count;

float roundedBoxSDF(vec2 center, vec2 size, float radius) {
    return length(max(abs(center) - size + radius, 0.0)) - radius;
}

void main() {
    vec2 center = gl_FragCoord.xy - position - half_size;
    float distance = roundedBoxSDF(center, half_size - half_thickness, radius + half_thickness);
    float smoothedAlphaOuter = 1.0 - smoothstep(-1.0, 1.0, distance - half_thickness);
    // Create an inner circle that isn't as anti-aliased as the outer ring
    float smoothedAlphaInner = 1.0 - smoothstep(-1.0, 0.5, distance + half_thickness);

    if (is_top_left && (center.y > 0.0 || center.x > 0.0)) {
        discard;
    } else if (is_top_right && (center.y > 0.0 || center.x < 0.0)) {
        discard;
    } else if (is_bottom_left && (center.y < 0.0 || center.x > 0.0)) {
        discard;
    } else if (is_bottom_right && (center.y < 0.0 || center.x < 0.0)) {
        discard;
    }

    gl_FragColor = mix(vec4(0), gradient(colors, count, size, grad_box, origin, degree, linear, blend), smoothedAlphaOuter - smoothedAlphaInner);
}
