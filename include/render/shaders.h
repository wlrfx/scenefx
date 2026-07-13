#ifndef _RENDERER_SHADERS_H
#define _RENDERER_SHADERS_H

// Note: Needs to be the same as in the tex.frag shader
enum fx_tex_shader_effects {
	SHADER_TEXTURE_EFFECT_NONE = 0,
	SHADER_TEXTURE_EFFECT_ROUND_CORNERS = 1 << 0,
	SHADER_TEXTURE_EFFECT_CLIPPING = 1 << 1,
	SHADER_TEXTURE_EFFECT_DISCARD_TRANSPARENT = 1 << 2,
	SHADER_TEXTURE_EFFECT_LAST = 1 << 3,
};

// Note: Needs to be the same as in the quad.frag shader
enum fx_quad_shader_effects {
	SHADER_QUAD_EFFECT_NONE = 0,
	SHADER_QUAD_EFFECT_ROUND_CORNERS = 1 << 0,
	SHADER_QUAD_EFFECT_CLIPPING = 1 << 1,
	SHADER_QUAD_EFFECT_LAST = 1 << 2,
};

#endif // !_RENDERER_SHADERS_H
