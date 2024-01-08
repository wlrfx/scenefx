wlr_files += files(
	'scene/wlr_scene.c',
)

if not wlroots_is_sub
	wlr_files += files([
		'output/wlr_output.c',
		'buffer/buffer.c',
	])
endif

subdir('fx')
