renderers = get_option('renderers')
if 'auto' in renderers and get_option('auto_features').enabled()
	renderers = ['gles2', 'vulkan']
elif 'auto' in renderers and get_option('auto_features').disabled()
	renderers = []
endif

wlr_files += files(
	'matrix.c',
	'fx_stencilbuffer.c',
	'fx_renderer.c',
)

if 'gles2' in renderers or 'auto' in renderers
	egl = dependency('egl', required: 'gles2' in renderers)
	gbm = dependency('gbm', required: 'gles2' in renderers)
	if egl.found() and gbm.found()
		wlr_deps += [egl, gbm]
		internal_features += { 'egl': true }
	endif
	subdir('gles2')
endif

