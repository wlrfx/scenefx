embed = find_program('./embed.sh', native: true)

# TODO: Validate each fragment shader just like upstream
shaders = files(
	'common.vert',
	'gradient.frag',
	'quad.frag',
	'quad_grad.frag',
	'quad_grad_round.frag',
	'tex.frag',
	'box_shadow.frag',
	'blur1.frag',
	'blur2.frag',
	'blur_effects.frag',
) + common_shaders

foreach shader : shaders
	name = fs.name(shader)
	output = name.underscorify() + '_src.h'
	var = name.underscorify() + '_src'
	scenefx_files += custom_target(
		output,
		command: [embed, var],
		input: shader,
		output: output,
		feed: true,
		capture: true,
	)
endforeach
