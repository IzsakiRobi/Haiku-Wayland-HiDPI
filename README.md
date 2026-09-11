# Haiku Wayland HiDPI noScale

This branch keeps the original fixed 1× Wayland rendering from
[X547/wayland-server](https://github.com/X547/wayland-server) and changes only
cursor handling. Wayland applications use Haiku's native system cursor, so the
cursor follows the desktop cursor-size setting.

It is intended as a clean base for testing Haiku `app_server` synchronization
without compositor-side UI scaling.

## Build on Haiku

```sh
pkgman install meson ninja wayland_devel wayland_protocols
meson setup build
ninja -C build
```

The resulting library is `build/wayland-server-inproc.so`.

## Install for testing

Back up the currently installed library, then copy the build:

```sh
cp /boot/home/config/non-packaged/lib/wayland-server-inproc.so \
  /boot/home/config/non-packaged/lib/wayland-server-inproc.so.backup
cp build/wayland-server-inproc.so \
  /boot/home/config/non-packaged/lib/wayland-server-inproc.so
```

Restart running Wayland applications after replacing the library.

## License

MIT. See [License.md](License.md). The original project is copyright X512 and
its contributors.
