# Only needed for drm_fourcc.h
libdrm_header = dependency('libdrm').partial_dependency(compile_args: true, includes: true)

compositors = {
	'scene-graph': {
		'src': 'scene-graph.c',
		'proto': ['xdg-shell'],
	},
}

foreach name, info : compositors
	extra_src = []
	foreach p : info.get('proto', [])
		extra_src += protocols_server_header[p]
	endforeach

	executable(
		name,
		[info.get('src'), extra_src],
		dependencies: [scenefx, libdrm_header, info.get('dep', [])],
		build_by_default: get_option('examples'),
	)
endforeach

