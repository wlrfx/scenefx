glesv2 = dependency('glesv2', required: 'gles2' in renderers)

if not (glesv2.found() and internal_features['egl'])
	subdir_done()
endif

glslang = find_program('glslang', 'glslangValidator', native: true, required: false, disabler: true)

features += { 'gles2-renderer': true }
scenefx_deps += glesv2

scenefx_files += files(
	'buffer.c',
	'egl.c',
	'pass.c',
	'renderer.c',
	'shaders.c',
	'tracy.c',
)

subdir('shaders')
