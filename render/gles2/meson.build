egl = dependency('egl', required: true)
eglext_version = cc.get_define(
	'EGL_EGLEXT_VERSION',
	dependencies: egl,
	prefix: '#include <EGL/eglext.h>',
).to_int()
if eglext_version < 20210604
	egl = dependency(
		'',
		required: true,
		not_found_message: 'EGL headers too old',
	)
endif
gbm = dependency('gbm', required: true)
glesv2 = dependency('glesv2', required: true)

glslang = find_program('glslang', 'glslangValidator', native: true, required: false, disabler: true)

internal_features += {'egl': true}
features += { 'gles2-renderer': true }
has_renderers = true

scenefx_deps += [egl, gbm, glesv2]

scenefx_files += files(
	'buffer.c',
	'egl.c',
	'pass.c',
	'renderer.c',
	'shaders.c',
	'tracy.c',
)

subdir('shaders')
