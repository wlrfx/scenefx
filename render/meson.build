scenefx_files += files(
	'color.c',
	'fx_offscreen_buffers.c',
	'fx_pass.c',
	'fx_renderer.c',
	'pixel_format.c',
	'tracy.c',
)

if wlroots.get_variable('have_gles2_renderer') == 'true'
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
