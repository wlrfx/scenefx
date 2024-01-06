project(
	'scenefx',
	'c',
	version: '0.1',
	license: 'MIT',
	meson_version: '>=0.59.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

# When doing a major or minor release, *always* increase soversion. This isn't
# necessary for bugfix releases. Increasing soversion is required because
# wlroots never guarantees ABI stability -- only API stability is guaranteed
# between minor releases.
soversion = 1

little_endian = target_machine.endian() == 'little'
big_endian = target_machine.endian() == 'big'

add_project_arguments([
	'-DWLR_USE_UNSTABLE',
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

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.global_build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
	if i >= source_root.length() or not in_prefix or p != source_root[i]
		in_prefix = false
		relative_dir_parts += '..'
	endif
	i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
	if i >= build_root.length() or not in_prefix or build_root[i] != p
		in_prefix = false
		relative_dir_parts += p
	endif
	i += 1
endforeach
relative_dir = join_paths(relative_dir_parts) + '/'

# Strip relative path prefixes from the code if possible, otherwise hide them.
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

wayland_project_options = ['tests=false', 'documentation=false']
wayland_server = dependency('wayland-server',
	version: '>=1.22',
	fallback: 'wayland',
	default_options: wayland_project_options,
)

wlroots_options = [ 'examples=false' ]
wlroots_version = ['>=0.17.0', '<0.18.0']
subproject(
	'wlroots',
	default_options: wlroots_options,
	required: false,
	version: wlroots_version,
)
wlroots = dependency('wlroots',
	version: wlroots_version,
	default_options: wlroots_options,
)

drm = dependency('libdrm',
	version: '>=2.4.114',
	fallback: 'libdrm',
	default_options: [
		'intel=disabled',
		'radeon=disabled',
		'amdgpu=disabled',
		'nouveau=disabled',
		'vmwgfx=disabled',
		'omap=disabled',
		'exynos=disabled',
		'freedreno=disabled',
		'tegra=disabled',
		'vc4=disabled',
		'etnaviv=disabled',
		'cairo-tests=disabled',
		'man-pages=disabled',
		'valgrind=disabled',
		'tests=false',
	],
)
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1',
	version: '>=0.42.0',
	fallback: 'pixman',
	default_options: ['werror=false'],
)
math = cc.find_library('m')
rt = cc.find_library('rt')

wlr_files = []
wlr_deps = [
	wlroots,
	wayland_server,
	drm,
	xkbcommon,
	pixman,
	math,
	rt,
]

subdir('protocol')
subdir('render')

subdir('types')
subdir('util')

subdir('include')

scenefx_inc = include_directories('include')

lib_scenefx = library(
	meson.project_name(), wlr_files,
	soversion: soversion.to_string(),
	dependencies: wlr_deps,
	include_directories:  [ scenefx_inc ],
	install: true,
)


scenefx = declare_dependency(
	link_with: lib_scenefx,
	dependencies: wlr_deps,
	include_directories: scenefx_inc,
)

meson.override_dependency('scenefx', scenefx)

if get_option('examples')
	# TODO:	subdir('examples')
	subdir('tinywl')
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(lib_scenefx,
	version: meson.project_version(),
	filebase: meson.project_name(),
	name: meson.project_name(),
	description: 'Wlroots effects library',
)
