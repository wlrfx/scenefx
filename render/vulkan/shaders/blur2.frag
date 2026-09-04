#version 450

// Dual-Kawase upsample pass. Port of the GLES2 blur2.frag.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform UBO {
	layout(offset = 80) vec2 halfpixel;
	float radius;
} data;

void main() {
	vec2 suv = uv / 2.0;
	vec2 hp = data.halfpixel;
	float r = data.radius;

	vec4 sum = texture(tex, suv + vec2(-hp.x * 2.0, 0.0) * r);

	sum += texture(tex, suv + vec2(-hp.x, hp.y) * r) * 2.0;
	sum += texture(tex, suv + vec2(0.0, hp.y * 2.0) * r);
	sum += texture(tex, suv + vec2(hp.x, hp.y) * r) * 2.0;
	sum += texture(tex, suv + vec2(hp.x * 2.0, 0.0) * r);
	sum += texture(tex, suv + vec2(hp.x, -hp.y) * r) * 2.0;
	sum += texture(tex, suv + vec2(0.0, -hp.y * 2.0) * r);
	sum += texture(tex, suv + vec2(-hp.x, -hp.y) * r) * 2.0;

	out_color = sum / 12.0;
}
