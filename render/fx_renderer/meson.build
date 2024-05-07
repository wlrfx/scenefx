renderers = get_option('renderers')
if 'auto' in renderers and get_option('auto_features').enabled()
	renderers = ['gles2']
elif 'auto' in renderers and get_option('auto_features').disabled()
	renderers = []
endif

wlr_files += files(
	'util.c',
	'shaders.c',
	'pixel_format.c',
	'fx_pass.c',
	'fx_framebuffer.c',
	'fx_effect_framebuffers.c',
	'fx_texture.c',
	'fx_renderer.c',
)

if not wlroots_is_sub
	wlr_files += files([
		'matrix.c',
	])
endif

if 'gles2' in renderers or 'auto' in renderers
	egl = dependency('egl', required: 'gles2' in renderers)
	gbm = dependency('gbm', required: 'gles2' in renderers)
	glesv2 = dependency('glesv2', required: 'gles2' in renderers)

	if egl.found() and gbm.found() and glesv2.found()
		wlr_deps += [egl, gbm, glesv2]
	endif
	subdir('gles2')
endif

