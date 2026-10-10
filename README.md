# scenefx

wlroots is the de-facto library for building wayland compositors, and its scene api has vastly simplified compositor development. However, for developers wanting modern aesthetic flair, the Scene API forces the use of the standard wlr_renderer, which is intentionally kept minimal and simple. SceneFX bridges this gap by serving as a drop-in replacement for the Scene API. It swaps out the default renderer for our custom FX renderer, allowing compositors to easily render eye-candy effects like blur, shadows, and rounded corners while maintaining the simplicity of the upstream Scene API.

## Compositors Using SceneFX
Plenty of popular wayland compositors are using SceneFX to render eyecandy, including:
- [SwayFX](https://github.com/WillPower3309/swayfx)
- [Mango](https://github.com/mangowm/mango)
- [Umbriel](https://github.com/noctalia-dev/umbriel)
- [nauka](https://github.com/shadowash8/nauka)
- [mwc](https://github.com/nikoloc/mwc)
- dwl [with a patch](https://codeberg.org/dwl/dwl-patches/src/branch/main/stale-patches/scenefx)

## Installation
<a href="https://repology.org/project/scenefx/versions"><img src="https://repology.org/badge/vertical-allrepos/scenefx.svg"/></a>

Scenefx is also available in <a href="https://copr.fedorainfracloud.org/coprs/damiand/scenefx/">copr</a> repositories. Enable via `dnf copr enable damiand/scenefx` and install via `dnf install scenefx scenefx-devel`

## Compiling From Source
Install dependencies:
* meson \*
* wlroots
* wayland
* wayland-protocols \*
* EGL and GLESv2
* libdrm
* pixman

_\* Compile-time dep_

Run these commands:
```sh
meson setup build/
ninja -C build/
```

Install like so:
```sh
sudo ninja -C build/ install
```

## Troubleshooting

### Using scenefx features breaks the compositor

This issue might be caused by compiling scenefx and wlroots with
Clang compiler and thin LTO(`-flto=thin`) option enabled. Try to
compile the libraries without LTO optimizations or with GCC compiler instead.

## Debugging

SceneFX includes the same debugging tools and environment variables as upstream wlroots does, but with some extra goodies.

### Environment variables:

- `WLR_RENDERER_ALLOW_SOFTWARE=1`: Use software rendering
- `WLR_SCENE_DEBUG_DAMAGE=rerender`: Re-render the whole display on each commit (don't use damage)
- `WLR_SCENE_DEBUG_DAMAGE=highlight`: Highlights where damage has occurred (where SwayFX get's re-rendered)
- `WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`: Disable direct scanout (always composites, even fullscreen windows)
- `WLR_SCENE_DISABLE_VISIBILITY=1`: Disables culling of non-visible regions of a window/buffer (an example would be a small window fully covered by an opaque window)
- `WLR_SCENE_HIGHLIGHT_TRANSPARENT_REGION=1`: Highlights the transparent areas of a window/buffer
- `WLR_EGL_NO_MODIFIERS=1`: Disables modifiers for EGL

### Tracy profiling

Optional [Tracy](https://github.com/wolfpld/tracy) profiling can be enabled for an extra good view of when and what is happening.

#### Enabling:

Note: These instructions will enable basic profiling

1. Add SceneFX as a subproject to your compositor
2. Import it as a subproject dependency
3. Run `meson subprojects download` to download the tracy project into the subproject directory
4. Compile SceneFX with `-Dtracy_enable=true` (and `--buildtype=debugoptimized` if using meson).
5. Start your compositor
6. Start the `tracy-profiler` and connect to the running compositor (The version of the profiler should not matter, but if any issues are encountered, try using a version that matches the subproject).

To enable more advanced profiling, the compositor in question needs to be run by the root user which comes with its own drawbacks, like DBus not working out of the box. A recommended way of doing this is by running your compositor with the `-E` sudo flag, such as `sudo -E sway`.

To enable DBus, you need to give the root user access to the DBus session bus by adding the following lines to the `/etc/dbus-1/session-local.conf` file (you might have to create said file), and reboot your system.

```xml
<!-- /etc/dbus-1/session-local.conf -->
<!-- Allow root to access session bus -->
<busconfig>
  <policy context="mandatory">
    <allow user="root"/>
  </policy>
</busconfig>
```

#### Additional tracy documentation

The links below are helpful when learning how to use tracy:

- https://github.com/wolfpld/tracy/releases/latest/download/tracy.pdf
- https://www.youtube.com/watch?v=ghXk3Bk5F2U

---
[Join our Discord](https://discord.gg/qsSx397rkh)

[Check out the wiki](https://wlrfx.github.io/wiki/data/scenefx/index.html)
