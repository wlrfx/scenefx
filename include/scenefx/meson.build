conf_data = configuration_data()
foreach name, have : features
	conf_data.set10('SCENEFX_HAS_' + name.underscorify().to_upper(), have)
endforeach
conf_data.set10('SCENEFX_HAS_ANY_RENDERERS', has_renderers)

conf_h = configure_file(
	input: 'config.h.in',
	output: 'config.h',
	configuration: conf_data,
)

install_headers(conf_h, subdir: versioned_name / 'scenefx')
