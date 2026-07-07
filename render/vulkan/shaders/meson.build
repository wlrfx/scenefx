vulkan_shaders_src = [
	'common.vert',
	'quad.frag',
]

vulkan_shaders = []
foreach shader : vulkan_shaders_src
	name = shader.underscorify() + '_data'
	args = [
		glslang,
		'-I' + common_shaders_path,
		'-V', '@INPUT@',
		'-o', '@OUTPUT@',
		'--vn', name,
	]
	if glslang_version.version_compare('>=11.0.0')
		args += '--quiet'
	endif

	header = custom_target(
		shader + '_spv',
		output: shader + '.h',
		input: shader,
		command: args,
		depend_files: common_shaders,
	)

	vulkan_shaders += [header]
endforeach

scenefx_files += vulkan_shaders
