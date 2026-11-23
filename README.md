# Stellar WiM

**Stellar WiM** is a lightweight Wayland window manager and compositor forked
from [Labwc](https://github.com/labwc/labwc) 0.20.3. It keeps Labwc's
architecture, stability and configuration compatibility, and adds optional
capabilities that remain completely inert when disabled.

> This is a real source fork, not a wrapper or a visual mock-up. The C source,
> Meson build system, protocols and workspace model are inherited from Labwc
> and modified in place.

## Current status

| Feature | Status |
| --- | --- |
| Full Labwc 0.20.3 compositor (floating windows, decorations, workspaces, input, XWayland) | Working, inherited |
| Independent identity (`stellar-wim`, `~/.config/stellar-wim`, own session/portals/icons/man pages) | Working |
| Stellar IPC / `stellar-wimctl` | Working (Fase 2) |
| Stellar Configuration API (validated, atomic `CONFIG` commands) | Working |
| Tabbed window groups (2–32 members, clickable bar, shared geometry) | Working (opt-in) |
| Optional tiling (5 modes, configurable geometry, per-window rules) | Working (opt-in) |
| `minimal` / `enhanced` profiles (tabs, tiling and effects) | Working (opt-in) |
| Visual effects (fade-in/out, opacity, inherited shadows) | Working (on by default) |
| Coexistent packaging, session target and measured idle baseline | Working |
| Blur, workspace-switch animation and geometry interpolation | Not implemented |

Only features marked *Working* are implemented in this revision. See
[CHANGELOG.md](CHANGELOG.md) for the exact state of each release and
[docs/ROADMAP.md](docs/ROADMAP.md) for the development plan.

## Relationship with Labwc

Stellar WiM is derived from Labwc and is licensed under **GPL-2.0-only**, the
same license as the upstream project. Copyright notices of the original authors
are preserved in the source files. See [LICENSE](LICENSE) and
[NOTICE.md](NOTICE.md).

Configuration compatibility is intentional:

* `<labwc_config>` remains the XML root element.
* `rc.xml`, `menu.xml`, `autostart`, `shutdown`, `environment` and themes are
  read from Labwc's locations as read-only fallbacks.
* Stellar WiM uses `~/.config/stellar-wim/` first and never writes into the
  Labwc configuration directory.
* Executables, session files, portal configuration, man pages, icons and
  D-Bus/portal names are installed under `stellar-wim` names, so both
  compositors can coexist on the same system.

## Building

Requirements (Ubuntu/Debian names):

```sh
sudo apt install meson ninja-build wayland-protocols libwayland-dev \
  libxkbcommon-dev libinput-dev libdrm-dev libgbm-dev libudev-dev libseat-dev \
  libxml2-dev glib2.0-dev libcairo2-dev libpango1.0-dev libpixman-1-dev \
  libpng-dev librsvg2-dev libxcb1-dev libxcb-ewmh-dev libxcb-icccm4-dev \
  libxcb-res0-dev libsfdo-dev gettext scdoc
```

wlroots 0.20.x is resolved by Meson from `subprojects/wlroots.wrap`; a system
installation of `wlroots-0.20 >= 0.20.1` also works.

```sh
meson setup build stellar-wim
ninja -C build
meson test -C build --print-errorlogs   # add -Dtest=enabled when setting up
sudo ninja -C build install
```

To uninstall: `sudo ninja -C build uninstall`.

Debian/Ubuntu metadata is in the root `debian/` directory; `sh
packaging/build-deb.sh` produces a `.deb` from a Meson dist archive. For every
install (not just Debian packages), a Meson install script removes all
build-only subproject destinations from Meson's install plan, so fallback
libraries never collide with system packages. See `packaging/README.md`. The
optional systemd user target is installed only when systemd support is
available at build time. See stellar-wim-session(7).

## Running

```sh
stellar-wim --help
stellar-wim --version
```

A Wayland session and a valid `XDG_RUNTIME_DIR` are required. The
`data/stellar-wim.desktop` session entry is installed into
`${datadir}/wayland-sessions` and can be selected from a display manager.
`LABWC_PID`/`LABWC_VER` are still exported for inherited scripts; the fork also
exports `STELLAR_WIM_PID`/`STELLAR_WIM_VER`.

### Stellar IPC

While the compositor is running, `stellar-wimctl` can inspect and control the
session through a local, uid-checked Unix socket:

```sh
stellar-wimctl version
stellar-wimctl windows list
stellar-wimctl desktops list
stellar-wimctl desktops switch 2
stellar-wimctl events subscribe
```

### Stellar Configuration API

External applications must not write `rc.xml` directly. The compositor is the
only writer: `CONFIG GET` and `CONFIG LIST` read live state. `CONFIG STATUS`
checks writability on the configuration worker,
while `CONFIG SET`, `CONFIG RESET`, `CONFIG KEYBINDINGS` and `CONFIG RULES`
validate requests on the main thread and queue XML/disk work to one serialized
worker. Replies and `config` events are emitted from the main event loop after
the write completes. Structural changes are reloaded from the event loop behind
the queued writes. Only argument-free compositor actions and known window-rule
properties are accepted; no shell commands or XML fragments can be injected.
See stellar-wim-ipc(7) for the complete reference.

Stellar Settings, Stellar Panel and the standalone `stellar-wim-settings`
application use this same API.

### Optional tabs and tiling

Both features are off by default. While they are off, no group objects, scene
nodes, timers or render loops are created for them.

```xml
<labwc_config>
  <stellar-wim>
    <tabs enabled="yes"/>
    <tiling enabled="yes">
      <mode>master-stack</mode>
      <master-ratio>0.60</master-ratio>
      <columns>2</columns>
      <min-width>0</min-width>
      <min-height>0</min-height>
      <max-width>0</max-width>
      <max-height>0</max-height>
    </tiling>
  </stellar-wim>
  <windowRules>
    <windowRule identifier="foot">
      <stellarTilingMode>horizontal</stellarTilingMode>
    </windowRule>
  </windowRules>
</labwc_config>
```

With tabs enabled, `StellarTabGroup` groups the focused window with the next
focusable window on the same desktop, `StellarTabNext` /
`StellarTabPrevious` switch tabs, and `StellarTabDetach` removes a window from
its group. IPC `tabs group <id> <id> [id...]` accepts 2–32 window ids and can
extend an existing group. The tab-bar segments are clickable: clicking one
activates that tab. Group members share one geometry, so moving or resizing
the active tab applies to every member. Only the active tab is visible; the
others stay mapped but hidden once their fade-out completes. The same
operations are available over IPC (`tabs group`, `tabs next`, `tabs previous`,
`tabs detach`, `tabs list`, `tabs status`).

With tiling enabled, normal mapped windows are arranged on each output using
one of five modes: `master-stack` (the default), `horizontal`, `vertical`,
`grid` and `hybrid`. `master-ratio` defaults to `0.60` and is clamped to
`0.2`–`0.8`; `columns` defaults to `2` and is at least `1`. The four
min/max width/height options default to `0`, meaning no limit. A per-window
`stellarTilingMode` rule selects the mode for the whole output; the last
matching rule wins. `stellar-wimctl tiling set <mode>` changes the mode at
runtime, and `stellar-wimctl tiling status` reports `enabled`, `managed`,
`mode`, `master_ratio` and `columns`. `StellarTilingToggle` switches tiling at
runtime and `StellarTilingReapply` recomputes the layout. Disabling tiling
leaves windows at their current geometry; they become freely movable again.

Both features add no new action names inherited from Labwc: all new actions
use the `Stellar` prefix. See `stellar-wim-actions(5)` and
`stellar-wim-ipc(7)` for details.

### Profiles

A profile is a preset over the safe runtime toggles above:

```xml
<labwc_config>
  <stellar-wim>
    <profile>enhanced</profile>
  </stellar-wim>
</labwc_config>
```

* `minimal` — tabs, tiling, animations, opacity and shadows off.
* `enhanced` — tabs, tiling, animations, opacity and shadows on; the opacity
  level is restored to `0.90` if it was zero.
* `custom` — no preset; the individual `<tabs>` / `<tiling>` / `<effects>`
  flags and `<theme><dropShadows>` decide. This is the default, so existing
  0.3.x configurations keep their own flags.

IPC stays available in both named profiles: it is cheap and gives you a way to
switch back. `stellar-wimctl profile get` reports the active profile and
`profile set minimal|enhanced` changes it at runtime. The actions
`StellarProfileMinimal` and `StellarProfileEnhanced` do the same from keybinds.

Only switches that are safe while running are performed: renderer, adaptive
sync, XWayland and protocol setup are never changed by a profile. Changing the
tiling mode or enabling/disabling tiling by hand (`StellarTilingToggle` or
`stellar-wimctl tiling set`) marks the session `custom`; SIGHUP re-applies
whatever is configured in `rc.xml`.

### Visual effects

Animations, drop shadows and opacity are **enabled by default** in Stellar WiM.
They are also part of the `minimal` and `enhanced` profiles: `minimal` disables
all three and `enhanced` restores them. To turn them off explicitly:

```xml
<labwc_config>
  <theme>
    <dropShadows>no</dropShadows>
  </theme>
  <stellar-wim>
    <effects animations="no" opacity="no" opacity-level="0.90"/>
  </stellar-wim>
</labwc_config>
```

* `animations` — a short fade-in when a window maps, plus a fade-out when a
  window is minimized or a tab is hidden. While a fade-out runs, the view's
  scene node stays enabled and its visibility change is deferred until the
  animation completes. One timer source exists only while a fade is running;
  at rest there is no timer and no additional render loop.
* `opacity` — uniform opacity for client content buffers, re-applied after
  every surface commit because wlroots resets it. Decorations are untouched.
* `opacity-level` — value in `0.00`–`1.00` (default `0.90`), clamped if out of
  range.

Drop shadows are not reimplemented: they are Labwc's inherited
`<theme><dropShadows>` option, which Stellar WiM sets to `yes` by default.
They create no scene nodes when disabled. Blur is unavailable because wlroots
0.20 has no scene blur API, and workspace-switch animation or window-geometry
interpolation is not implemented. `stellar-wimctl effects status` reports the
real flags plus live counters (`active_animations`, `timer_armed`,
`frame_commits`, `animations_started`, which includes fade-outs).

The socket is created in `$XDG_RUNTIME_DIR` with mode 0600. IPC can be disabled
in `rc.xml` without affecting the rest of the compositor:

```xml
<labwc_config>
  <stellar-wim>
    <ipc enabled="false"/>
  </stellar-wim>
</labwc_config>
```

## Documentation

* `stellar-wim(1)` — invocation, signals, environment
* `stellar-wimctl(1)` — local control client
* `stellar-wim-ipc(7)` — socket protocol, limits and security model
* `stellar-wim-config(5)` — inherited rc.xml schema and Stellar extensions
* `stellar-wim-menu(5)`, `stellar-wim-theme(5)`, `stellar-wim-actions.5`
* `stellar-wim-philosophy(7)` — upstream design principles (credited)
* `stellar-wim-session(7)` — session integration and Labwc coexistence
* [docs/UPSTREAM-LABWC-README.md](docs/UPSTREAM-LABWC-README.md) — original upstream README

## Contributing / verification

Compile, run the test suite and report which checks could not be performed in
your environment. Do not claim a feature is complete without a build and a
test. The upstream contribution guide is kept at
[CONTRIBUTING.md](CONTRIBUTING.md). `t/benchmark.sh` compares an idle headless
Stellar WiM with an unmodified Labwc 0.20.3 build; see
[docs/VERIFICATION.md](docs/VERIFICATION.md) for measured results and their
limits.
