renderers = get_option('renderers')
if 'auto' in renderers and get_option('auto_features').enabled()
	renderers = ['gles2']
elif 'auto' in renderers and get_option('auto_features').disabled()
	renderers = []
endif

scenefx_files += files(
	'color.c',
	'fx_offscreen_buffers.c',
	'fx_pass.c',
	'fx_renderer.c',
	'pixel_format.c',
	'tracy.c',
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
	if egl.found() and gbm.found()
		scenefx_deps += [egl, gbm]
		internal_features += {'egl': true}
	endif

	subdir('gles2')
endif

lcms2 = dependency('lcms2', required: get_option('color-management'))
if lcms2.found()
	scenefx_deps += lcms2
	scenefx_files += files('color_lcms2.c')
	features += {'color-management': true}
else
	scenefx_files += files('color_fallback.c')
endif
