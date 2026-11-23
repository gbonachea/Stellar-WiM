# Packaging Stellar WiM

Stellar WiM is built with Meson and installs under its own names, so it can be
installed side by side with Labwc. Nothing in the install tree is called
`labwc`: the executable, CLI, session file, portal config, icons, man pages and
example configuration all use the `stellar-wim` prefix.

## Generic installation

```sh
meson setup build-stellar . -Dtest=enabled
ninja -C build-stellar
meson install -C build-stellar
```

Use `DESTDIR` (or `meson install --destdir`) for staged installs. The install
layout is:

| Path | Content |
| --- | --- |
| `bindir/stellar-wim` | compositor |
| `bindir/stellar-wimctl` | IPC client |
| `datadir/wayland-sessions/stellar-wim.desktop` | display-manager session |
| `datadir/xdg-desktop-portal/stellar-wim-portals.conf` | portal preferences |
| `datadir/icons/hicolor/scalable/apps/stellar-wim*.svg` | session icon |
| `mandir/man1/stellar-wim.1` and others | manual pages |
| `datadir/doc/stellar-wim/` | example configuration and session helpers |
| systemd user unit dir / `stellar-wim-session.target` | session target (when systemd is available) |

The configuration directory is `$XDG_CONFIG_HOME/stellar-wim` (normally
`~/.config/stellar-wim`), never `~/.config/labwc`. Existing Labwc files are
read as a fallback only when no Stellar configuration exists.

## Debian / Ubuntu

The Debian metadata lives in the repository root as `debian/`, so a Meson
source archive is already a valid Debian source tree. The easiest build is:

```sh
sh packaging/build-deb.sh
```

The script creates the archive with `meson dist --include-subprojects`, so the
source tree contains wlroots and its subprojects and builds offline with the
`--wrap-mode=nodownload` that debhelper uses. It extracts the archive in a
directory without spaces, provides the properly named orig tarball and runs
`dpkg-buildpackage -us -uc -b`. It prints the path of the resulting `.deb` and
copies it to the repository root.

Equivalent manual steps:

```sh
meson dist -C build-stellar --allow-dirty
cd /tmp
tar -xf "/path/to/build-stellar/meson-dist/stellar-wim-0.8.0.tar.xz"
cp .../stellar-wim-0.8.0.tar.xz stellar-wim_0.8.0.orig.tar.xz
cd stellar-wim-0.8.0
dpkg-buildpackage -us -uc -b
```

The package build uses `-Ddefault_library=static`. Isolation from subprojects
is handled by Meson itself, not by a Debian-only `rm -rf usr/lib`: the install
script `build-aux/meson/strip-subproject-install.py` reads Meson's install
plan and removes *every* destination belonging to a fallback subproject,
including libraries, utilities, headers, pkg-config files, man pages and
data, then prunes the empty directories those destinations leave behind.
Labwc's `labnag` and `lab-sensible-terminal` helpers stay in the source tree for
upstream parity but are not marked for installation. This applies to staged
installs and generic `meson install` runs too, so build-only artifacts never
collide with system packages such as `libv4l-0t64` and `libliftoff0`.

`-Dtest=disabled` is used for the package build so cmocka is not a build
dependency. wlroots 0.20 is provided by the Meson wrap in `subprojects/`, so it
is deliberately not listed in `Build-Depends`; `git` and `ca-certificates` are
listed instead because the wrap is fetched with git. A packager who prefers a
fully offline build can install a packaged wlroots 0.20 and pass
`--wrap-mode=nodownload` to Meson. `libsystemd-dev` is optional: the systemd
user target is installed only when systemd support is found at build time.

The Debian package does not declare any conflict with `labwc`, because the two
projects install different binaries, sessions and configuration directories.

## Other distributions

Meson's install step is distribution-agnostic. A packager only needs to map
the paths above to the distribution's conventions. No patches are required to
avoid collisions with Labwc.
