renderers = get_option('renderers')
if 'auto' in renderers and get_option('auto_features').enabled()
	renderers = ['gles2']
elif 'auto' in renderers and get_option('auto_features').disabled()
	renderers = []
endif

scenefx_files += files(
	'util.c',
	'shaders.c',
	'pixel_format.c',
	'fx_pass.c',
	'fx_framebuffer.c',
	'fx_effect_framebuffers.c',
	'fx_texture.c',
	'fx_renderer.c',
)

if 'gles2' in renderers or 'auto' in renderers
	egl = dependency('egl', required: 'gles2' in renderers)
	if egl.found()
		eglext_version = cc.get_define(
			'EGL_EGLEXT_VERSION',
			dependencies: egl,
			prefix: '#include <EGL/eglext.h>',
		).to_int()
		if eglext_version < 20210604
			egl = dependency(
				'',
				required: 'gles2' in renderers,
				not_found_message: 'EGL headers too old',
			)
		endif
	endif
	gbm = dependency('gbm', required: 'gles2' in renderers)
	glesv2 = dependency('glesv2', required: 'gles2' in renderers)

	glslang = find_program('glslang', 'glslangValidator', native: true, required: false, disabler: true)

	if egl.found() and gbm.found() and glesv2.found()
		scenefx_deps += [egl, gbm, glesv2]
	endif
	subdir('gles3')
	subdir('gles2')
endif

