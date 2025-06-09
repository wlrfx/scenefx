#version 300 es

uniform mat3 proj;
uniform vec4 color;
uniform mat3 tex_proj;
in vec2 pos;
out vec4 v_color;
out vec2 v_texcoord;

void main() {
	vec3 pos3 = vec3(pos, 1.0);
	gl_Position = vec4(pos3 * proj, 1.0);
	v_color = color;
	v_texcoord = (pos3 * tex_proj).xy;
}
