wayland_protos = dependency('wayland-protocols',
	version: '>=1.35',
	fallback: 'wayland-protocols',
	default_options: ['tests=false'],
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
	wayland_scanner_dep.get_variable('wayland_scanner'),
	native: true,
)

protocols = {
	# Stable upstream protocols
	'linux-dmabuf-v1': wl_protocol_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml',
	'presentation-time': wl_protocol_dir / 'stable/presentation-time/presentation-time.xml',
	'tablet-v2': wl_protocol_dir / 'stable/tablet/tablet-v2.xml',
	'viewporter': wl_protocol_dir / 'stable/viewporter/viewporter.xml',
	'xdg-shell': wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',

	# Staging upstream protocols
	'alpha-modifier-v1': wl_protocol_dir / 'staging/alpha-modifier/alpha-modifier-v1.xml',
	'content-type-v1': wl_protocol_dir / 'staging/content-type/content-type-v1.xml',
	'cursor-shape-v1': wl_protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
	'drm-lease-v1': wl_protocol_dir / 'staging/drm-lease/drm-lease-v1.xml',
	'ext-foreign-toplevel-list-v1': wl_protocol_dir / 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml',
	'ext-idle-notify-v1': wl_protocol_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml',
	'ext-session-lock-v1': wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
	'fractional-scale-v1': wl_protocol_dir / 'staging/fractional-scale/fractional-scale-v1.xml',
	'linux-drm-syncobj-v1': wl_protocol_dir / 'staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml',
	'security-context-v1': wl_protocol_dir / 'staging/security-context/security-context-v1.xml',
	'single-pixel-buffer-v1': wl_protocol_dir / 'staging/single-pixel-buffer/single-pixel-buffer-v1.xml',
	'xdg-activation-v1': wl_protocol_dir / 'staging/xdg-activation/xdg-activation-v1.xml',
	'xwayland-shell-v1': wl_protocol_dir / 'staging/xwayland-shell/xwayland-shell-v1.xml',
	'tearing-control-v1': wl_protocol_dir / 'staging/tearing-control/tearing-control-v1.xml',

	# Unstable upstream protocols
	'fullscreen-shell-unstable-v1': wl_protocol_dir / 'unstable/fullscreen-shell/fullscreen-shell-unstable-v1.xml',
	'idle-inhibit-unstable-v1': wl_protocol_dir / 'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml',
	'keyboard-shortcuts-inhibit-unstable-v1': wl_protocol_dir / 'unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml',
	'pointer-constraints-unstable-v1': wl_protocol_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml',
	'pointer-gestures-unstable-v1': wl_protocol_dir / 'unstable/pointer-gestures/pointer-gestures-unstable-v1.xml',
	'primary-selection-unstable-v1': wl_protocol_dir / 'unstable/primary-selection/primary-selection-unstable-v1.xml',
	'relative-pointer-unstable-v1': wl_protocol_dir / 'unstable/relative-pointer/relative-pointer-unstable-v1.xml',
	'text-input-unstable-v3': wl_protocol_dir / 'unstable/text-input/text-input-unstable-v3.xml',
	'xdg-decoration-unstable-v1': wl_protocol_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
	'xdg-foreign-unstable-v1': wl_protocol_dir / 'unstable/xdg-foreign/xdg-foreign-unstable-v1.xml',
	'xdg-foreign-unstable-v2': wl_protocol_dir / 'unstable/xdg-foreign/xdg-foreign-unstable-v2.xml',
	'xdg-output-unstable-v1': wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
	'ext-transient-seat-v1': wl_protocol_dir / 'staging/ext-transient-seat/ext-transient-seat-v1.xml',

	# Other protocols
	'drm': 'drm.xml',
	'input-method-unstable-v2': 'input-method-unstable-v2.xml',
	'kde-server-decoration': 'server-decoration.xml',
	'virtual-keyboard-unstable-v1': 'virtual-keyboard-unstable-v1.xml',
	'wlr-data-control-unstable-v1': 'wlr-data-control-unstable-v1.xml',
	'wlr-export-dmabuf-unstable-v1': 'wlr-export-dmabuf-unstable-v1.xml',
	'wlr-foreign-toplevel-management-unstable-v1': 'wlr-foreign-toplevel-management-unstable-v1.xml',
	'wlr-gamma-control-unstable-v1': 'wlr-gamma-control-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1': 'wlr-layer-shell-unstable-v1.xml',
	'wlr-output-management-unstable-v1': 'wlr-output-management-unstable-v1.xml',
	'wlr-output-power-management-unstable-v1': 'wlr-output-power-management-unstable-v1.xml',
	'wlr-screencopy-unstable-v1': 'wlr-screencopy-unstable-v1.xml',
	'wlr-virtual-pointer-unstable-v1': 'wlr-virtual-pointer-unstable-v1.xml',
}

protocols_code = {}
protocols_server_header = {}
protocols_client_header = {}
foreach name, path : protocols
	code = custom_target(
		name.underscorify() + '_c',
		input: path,
		output: '@BASENAME@-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)
	scenefx_files += code

	server_header = custom_target(
		name.underscorify() + '_server_h',
		input: path,
		output: '@BASENAME@-protocol.h',
		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
	)
	scenefx_files += server_header

	client_header = custom_target(
		name.underscorify() + '_client_h',
		input: path,
		output: '@BASENAME@-client-protocol.h',
		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
		build_by_default: false,
	)

	protocols_code += { name: code }
	protocols_server_header += { name: server_header }
	protocols_client_header += { name: client_header }
endforeach
