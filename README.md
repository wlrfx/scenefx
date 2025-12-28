# scenefx

wlroots is the de-facto library for building wayland compositors, and its scene api is a great stride in simplifying wayland compositor development. The problem with the scene api (for compositors looking for eye candy), however, is that it forces you to use the wlr renderer, which is powerful yet simple. SceneFX is a project that takes the scene api and replaces the wlr renderer with our own fx renderer, capable of rendering surfaces with eye-candy effects including blur, shadows, and rounded corners, while maintaining the benefits of simplicity gained from using the scene api.

**Please note: while SceneFX is in use by SwayFX version 0.4, it is not yet ready for usage by other compositors. Please refer to the [1.0 milestone](https://github.com/wlrfx/scenefx/milestone/2) to track the remaining tasks for our stable 1.0 release**

## Compositors Using SceneFX
Plenty of popular wayland compositors are using SceneFX to render eyecandy, including:
- [SwayFX](https://github.com/WillPower3309/swayfx)
- [MangoWC](https://github.com/DreamMaoMao/mangowc)
- [mwc](https://github.com/nikoloc/mwc)
- dwl [with a patch](https://codeberg.org/dwl/dwl-patches/src/branch/main/stale-patches/scenefx)

## Installation
<a href="https://repology.org/project/scenefx/versions"><img src="https://repology.org/badge/vertical-allrepos/scenefx.svg"/></a>


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

---
[Join our Discord](https://discord.gg/qsSx397rkh)
