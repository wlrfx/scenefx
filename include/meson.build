exclude_files = ['meson.build']

conf_data = configuration_data()
conf_data.set10('SCENEFX_HAS_GLES2_RENDERER', features.get('gles2-renderer'))
configure_file(
	input: 'config.h.in',
	output: 'config.h',
	configuration: conf_data,
)

install_subdir('scenefx',
	install_dir: get_option('includedir') / versioned_name,
	exclude_files: exclude_files,
)
