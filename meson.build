project(
	'scenefx',
	'c',
	version: '0.4.1',
	license: 'MIT',
	meson_version: '>=1.3',
	default_options: [
		'c_std=' + (meson.version().version_compare('>=1.4.0') ? 'c23,c11' : 'c11'),
		'warning_level=2',
		'werror=true',
		'wrap_mode=nodownload',
	],
)

version = meson.project_version().split('-')[0]
version_major = version.split('.')[0]
version_minor = version.split('.')[1]
versioned_name = '@0@-@1@.@2@'.format(meson.project_name(), version_major, version_minor)

little_endian = target_machine.endian() == 'little'
big_endian = target_machine.endian() == 'big'

add_project_arguments([
	'-D_POSIX_C_SOURCE=200809L',
	'-DWLR_USE_UNSTABLE',
	'-DWLR_PRIVATE=',
	'-DWLR_LITTLE_ENDIAN=@0@'.format(little_endian.to_int()),
	'-DWLR_BIG_ENDIAN=@0@'.format(big_endian.to_int()),
], language: 'c')

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wmissing-prototypes',
	'-Walloca',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

fs = import('fs')

# Strip relative path prefixes from the code if possible, otherwise hide them.
relative_dir = fs.relative_to(meson.current_source_dir(), meson.global_build_root()) + '/'
if cc.has_argument('-fmacro-prefix-map=/prefix/to/hide=')
	add_project_arguments(
		'-fmacro-prefix-map=@0@='.format(relative_dir),
		language: 'c',
	)
else
	add_project_arguments(
		'-D_WLR_REL_SRC_DIR="@0@"'.format(relative_dir),
		language: 'c',
	)
endif

wayland_kwargs = {
	'version': '>=1.23.1',
	'fallback': 'wayland',
	'default_options': [
		'tests=false',
		'documentation=false',
	],
}
wayland_server = dependency('wayland-server',
	kwargs: wayland_kwargs,
)

wlroots_options = [ 'examples=false' ]
wlroots_version = ['>=0.19.0', '<0.20.0']
wlroots = dependency('wlroots-0.19',
	version: wlroots_version,
	default_options: wlroots_options,
	required: false,
)
if not wlroots.found()
	wlroots_proj = subproject(
		'wlroots',
		default_options: wlroots_options,
		version: wlroots_version,
	)
	wlroots = wlroots_proj.get_variable('wlroots')
endif


drm = dependency('libdrm',
	version: '>=2.4.122',
	fallback: 'libdrm',
	default_options: [
		'auto_features=disabled',
		'tests=false',
	],
)
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1',
	version: '>=0.43.0',
	fallback: 'pixman',
	default_options: ['werror=false'],
)
math = cc.find_library('m')
rt = cc.find_library('rt')

scenefx_files = []
scenefx_deps = [
	wlroots,
	wayland_server,
	drm,
	xkbcommon,
	pixman,
	math,
	rt,
]

# Tracy
if get_option('tracy_enable')
	if get_option('buildtype') != 'debugoptimized'
		warning('Profiling builds should set --buildtype=debugoptimized')
	endif

	tracy_options = [
		'default_library=static',
		'werror=false',
		'warning_level=0',
	]
	tracy_proj = subproject(
		'tracy',
		required: true,
		default_options: tracy_options
	)
	scenefx_deps += tracy_proj.get_variable('tracy_dep')
	add_project_arguments(
		[
			'-DTRACY_ENABLE',
			'-DTRACY_ON_DEMAND',
		],
		language: 'c',
	)
endif

subdir('protocol')
subdir('render')

subdir('types')
subdir('util')

subdir('include')

scenefx_inc = include_directories('include')

lib_scenefx = library(
	versioned_name, scenefx_files,
	dependencies: scenefx_deps,
	include_directories:  [ scenefx_inc ],
	install: true,
)


scenefx = declare_dependency(
	link_with: lib_scenefx,
	dependencies: scenefx_deps,
	include_directories: scenefx_inc,
)

meson.override_dependency(versioned_name, scenefx)

if get_option('examples')
	subdir('examples')
	subdir('tinywl')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
	lib_scenefx,
	name: versioned_name,
	version: meson.project_version(),
	description: 'Wlroots effects library',
	subdirs: versioned_name,
)

summary({
	'tracy': get_option('tracy_enable'),
}, bool_yn: true)
