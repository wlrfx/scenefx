vulkan_shaders_src = {
	'common.vert': {'deps': []},
	'quad.frag': {
		'deps': ['corner_alpha.glsl'],
	},
}

vulkan_shaders = []
foreach shader, props : vulkan_shaders_src
	name = shader.underscorify() + '_data'
	args = [glslang, '-V', '@INPUT@', '-o', '@OUTPUT@', '--vn', name]
	if glslang_version.version_compare('>=11.0.0')
		args += '--quiet'
	endif

	header = custom_target(
		shader + '_spv',
		output: shader + '.h',
		input: shader,
		command: args,
		depend_files: files(props['deps']),
	)

	vulkan_shaders += [header]
endforeach

scenefx_files += vulkan_shaders
