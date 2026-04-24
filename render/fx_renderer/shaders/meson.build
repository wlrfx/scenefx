# glslang = find_program('glslang', 'glslangValidator', native: true, required: false, disabler: true)
# check = find_program('./check.sh', native: true)
embed = find_program('./embed.sh', native: true)

shaders = [
	'common.vert',
	'gradient.frag',
	'corner_alpha.frag',
	'quad.frag',
	'quad_grad.frag',
	'quad_round.frag',
	'quad_grad_round.frag',
	'tex.frag',
	'box_shadow.frag',
	'blur1.frag',
	'blur2.frag',
	'blur_effects.frag',
]

foreach name : shaders
	# TODO: Validate each fragment shader. Maybe combine them at build-time
	# instead of at runtime?

	# custom_target(
	# 	'gles2-' + name,
	# 	input: name,
	# 	output: name + '_check',
	# 	command: [check, glslang, '@INPUT@'],
	# 	capture: true,
	# 	build_by_default: true,
	# )

	output = name.underscorify() + '_src.h'
	var = name.underscorify() + '_src'
	scenefx_files += custom_target(
		output,
		command: [embed, var],
		input: name,
		output: output,
		feed: true,
		capture: true,
	)
endforeach

