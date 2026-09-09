# Haiku Wayland HiDPI

An in-process Wayland server for Haiku with desktop-font-based HiDPI scaling, native cursors and popup composition. Tested with Firefox, Thunderbird, GIMP and Inkscape on Haiku x86_64.

Based on [X512 (X547)'s wayland-server](https://github.com/X547/wayland-server). Original copyright is preserved under the [MIT license](License.md).

## Install

Download a ZIP or HPKG from [v0.2.0](https://github.com/IzsakiRobi/Haiku-Wayland-HiDPI/releases/tag/v0.2.0).

ZIP (backs up an existing library):

```sh
unzip haiway-hidpi-0.2.0-x86_64.zip
sh haiway-hidpi-0.2.0/install.sh
```

Or install the HPKG for the current user:

```sh
pkgman install -H ./wayland_server_hidpi-0.2.0-1-x86_64.hpkg
```

Use one method. A library previously installed in `~/config/non-packaged/lib` overrides the HPKG; move that library aside before switching to HPKG. Restart Wayland applications after installation or changing Haiku font sizes. Use default application scaling settings; no Firefox `devPixelsPerPx` override is needed.

## Build on Haiku

```sh
pkgman install meson ninja wayland_devel wayland_protocols
meson setup build
ninja -C build
```

Output: `build/wayland-server-inproc.so`. The release ZIP installer also accepts a locally built library placed beside it.

Popup transparency outside the parent window remains limited by Haiku's native window composition.
