# Haiku Wayland HiDPI

This is a patched build of [X547/wayland-server](https://github.com/X547/wayland-server), an in-process Wayland server implemented on top of the Haiku API.

The current source is based on upstream commit [`8ede02e`](https://github.com/X547/wayland-server/commit/8ede02e0e8801df4f23487c04b23e8f2c5569ad9).

## Changes

- Use Haiku's system cursor so Wayland applications follow the desktop cursor size on HiDPI displays.
- Composite surface trees through an offscreen bitmap.
- Reconstruct popup backgrounds from their parent Wayland surfaces, including nested popup chains.
- Prevent translucent popup shadows from accumulating during redraws.
- Preserve the upstream cursor flicker improvements.

## Known limitation

The part of a popup that extends outside its parent Wayland window uses Haiku's native fallback window background. Haiku does not currently provide per-pixel alpha-composited native windows, so the server cannot reproduce the desktop or another window behind that area without capturing the screen.

## Build on Haiku

Install the build dependencies:

```sh
pkgman install meson ninja wayland_devel wayland_protocols
```

Configure and build:

```sh
meson setup build
ninja -C build
```

The resulting library is `build/wayland-server-inproc.so`.

## Install

Download the ZIP or HPKG from the [v0.1.0 release](https://github.com/IzsakiRobi/Haiku-Wayland-HiDPI/releases/tag/v0.1.0).

The ZIP contains a backup-aware installer:

```sh
unzip wayland_server_hidpi-0.1.0-1-x86_64.zip
sh */install.sh
```

Or install the package for the current user:

```sh
pkgman install -H ./wayland_server_hidpi-0.1.0-1-x86_64.hpkg
```

Both methods install under the current user's Haiku configuration and leave `/boot/system` unchanged. Restart running Wayland applications after installation.

## Patch

[`patches/haiku-wayland-hidpi.patch`](patches/haiku-wayland-hidpi.patch) contains the complete change against the upstream base commit.

## License

MIT. See [License.md](License.md). The original project is copyright X512 and its contributors.
