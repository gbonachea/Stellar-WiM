# Changelog

All notable changes to Stellar WiM are documented here. Version numbers refer
to the fork, not to the upstream Labwc release.

## 0.8.0 (tabs, tiling and effects completion batch)

### Added

* Fade-out when a view is minimized or a tab is hidden. Visibility changes are
  deferred until the fade completes: the view stays scene-enabled (and
  *animations_started* counts the fade-out) until the animation finishes.
  Showing the view again cancels the fade-out and fades it back in.
* Arbitrary tab groups over IPC: `TABS GROUP <id>...` accepts 2..32 window ids,
  can extend an existing group, and exposes clickable tab-bar segments that
  activate the clicked tab. All members of a group share one geometry.
* Five tiling modes — `master-stack`, `horizontal`, `vertical`, `grid` and
  `hybrid` — with a configurable `master-ratio` (0.6, clamped to 0.2–0.8),
  `columns` (2, minimum 1) and min/max width/height limits (0 = unbounded).
  A per-window `stellarTilingMode` rule selects the mode for the whole output;
  the last matching rule wins.
* `TILING SET <mode>` changes the tiling mode at runtime; `TILING STATUS` now
  also reports `mode`, `master_ratio` and `columns`.
* The `minimal` and `enhanced` profiles now also toggle effects: `minimal`
  disables animations, opacity and shadows; `enhanced` restores them.
* `build-aux/meson/strip-subproject-install.py` runs from Meson's install step
  and removes every build-only subproject destination from Meson's install
  plan, for all installs rather than only Debian packages.
* Stellar Configuration API: `CONFIG GET/LIST/SET/RESET/STATUS/RELOAD` reads and
  changes validated settings through the existing IPC socket, writes `rc.xml`
  atomically and notifies subscribers with `config` events. XML parsing and
  disk writes run on one serialized worker so the compositor main thread never
  blocks; replies and events are delivered from the main event loop. `CONFIG
  KEYBINDINGS` and `CONFIG RULES` allow only argument-free compositor actions
  and known rule properties, never shell commands or XML fragments.
* Stellar Settings, Stellar Panel and a new standalone `stellar-wim-settings`
  application consume the same API through a shared Qt client.

### Changed

* Version bumped to 0.8.0; documentation, sample configurations and package
  metadata describe the new tiling/tab/effect behaviour and defaults.
* The "only map fade-in, no fade-out" statement in the 0.5.0 entry is
  historical: it described 0.5.0, while fade-out was added in this release.
  Blur, workspace-switch animation and geometry interpolation remain
  unimplemented; effects and IPC stay on by default, tabs and tiling off.

### Verification

* `test_stellar_features` adds coverage for mode parsing/names, tiling options
  and tab-group extension; `test_stellar_features_end_to_end` checks
  `TILING SET`, the enriched `TILING STATUS` fields and multi-window groups.
* `test_profiles_end_to_end` checks that profile switches also change effects,
  and `test_effects_end_to_end` observes a real minimize fade-out, including
  the `animations_started` counter.
* `test_packaging` checks that no build-only subproject paths survive a staged
  `meson install`. Results are recorded in docs/VERIFICATION.md.

## 0.7.0 (effects enabled by default)

### Changed

* `animations`, `opacity` and the inherited `theme/dropShadows` are now **on by
  default**. This was the intended behaviour of the project brief; 0.5.0 and
  0.6.0 shipped them off by default. `opacity-level` remains `0.90`.
* They can still be disabled explicitly with
  `<effects animations="no" opacity="no"/>`, and shadows with
  `<theme><dropShadows>no</dropShadows>`. The no-idle-cost guarantee is
  unchanged: with a feature disabled nothing is allocated, armed or rendered.
* `t/test-ipc.sh` and `t/test-effects.sh` now assert the new defaults and keep
  a separate case that explicitly disables all three effects.

### Notes

* Tabs, tiling and the `minimal`/`enhanced` profiles are still off/custom by
  default; only visual effects changed.
* Blur is still unavailable in wlroots 0.20.

## 0.6.0 (Fase 6 — packaging, session and measured baseline)

### Added

* Debian/Ubuntu source-package metadata in the repository-root `debian/`
  directory, plus `packaging/build-deb.sh` and generic instructions in
  `packaging/README.md`. The package does not conflict with `labwc`; all
  installed paths use the `stellar-wim` prefix.
* Meson dist archives are self-contained: the top-level `subprojects/` wraps
  are real `wrap-git` definitions instead of `wrap-redirect` files pointing
  into subprojects that are not shipped in the archive. `--include-subprojects`
  is used for the Debian source archive so it builds with debhelper's default
  `--wrap-mode=nodownload`.
* The Debian build compiles subprojects statically and drops `usr/lib` from
  the package, avoiding collisions with system libraries such as `libv4l1`
  and `libliftoff0`.
* `stellar-wim-session(7)` documenting the Wayland session entry, portal
  preferences, optional systemd user target and coexistence with Labwc. The
  session desktop file now has `Type=Application` and `TryExec=stellar-wim`.
* `t/test-packaging.sh`, a staged-install test that checks the installed
  binaries, session file, portal config, man pages, icons and example config,
  and fails if any Labwc-named path is installed.
* `t/benchmark.sh`, a repeatable headless comparison helper reporting startup
  time, idle CPU time and resident memory against an unmodified Labwc 0.20.3
  build from the imported source commit.

### Notes

* No new runtime dependency: the benchmark and packaging files are not part of
  the compositor binary. The systemd target remains optional and is installed
  only when systemd support is found at build time.

### Verification

* `test_packaging` passed against a staged `meson install`, including the new
  session man page and the absence of Labwc-named installed files.
* Five headless runs per compositor: median startup to Wayland socket was
  179 ms for the Labwc baseline and 181 ms for Stellar WiM; idle CPU over 3 s
  was 0 ms for both; median RSS was 101560 KB and 101628 KB respectively.
  These are idle headless numbers, not a rendering benchmark.

## 0.5.0 (Fase 5 — opt-in visual effects)

### Added

* `<stellar-wim><effects animations="..." opacity="..." opacity-level="..."/></stellar-wim>`.
  Both flags default to `no` and `opacity-level` to `0.90` (clamped to 0–1).
  While they are off the module allocates nothing, arms no timer and adds no
  scene node.
* Map fade-in animation using one transient `wl_event_source` that is created
  on the first fade and removed as soon as the last one finishes. Only map
  fade-in is implemented; there is no fade-out, no workspace-switch animation
  and no geometry interpolation.
* Per-view content opacity via `wlr_scene_buffer_set_opacity()`, re-applied from
  the xdg-shell and XWayland commit handlers because wlroots resets buffer
  opacity on every surface commit. Decorations are not affected.
* Real state in `EFFECTS STATUS`: `animations`, `opacity`, `shadows`, `blur`,
  `enabled`, `active_animations`, `timer_armed`, `frame_commits`,
  `animations_started` and `opacity_level`. `VERSION.features.animations` and
  `CAPABILITIES.effects` now reflect the configured effects instead of being
  hardcoded to false/0.

### Notes

* Shadows were already implemented upstream as `<theme><dropShadows>` and are
  intentionally not reimplemented; their status is now reported accurately.
* Blur remains unavailable: wlroots 0.20 scene API has no blur operation.
* Effects are independent of the `minimal` / `enhanced` profiles, which keep
  controlling only tabs and tiling.

### Verification

* `test_effects` adds five unit tests for clamping, ease-out and animation
  start/tick/cancel/completion.
* `test_effects_end_to_end` starts a headless compositor, checks all effects
  are off by default, confirms no frame commits happen while idle both with
  effects off and on, enables effects over SIGHUP, checks the clamped opacity
  level, the `VERSION`/`CAPABILITIES` flags and that no timer remains armed
  after disabling them again.
* Live verification with a real xdg-shell client started two windows with
  animations enabled: two fades started, `timer_armed` became true, real frame
  commits advanced during the fade, and both the timer and active animation
  count returned to zero afterwards.

## 0.4.0 (Fase 4 — runtime profiles)

### Added

* Two opt-in presets, `minimal` (tabs and tiling off) and `enhanced` (both on),
  selected with `<stellar-wim><profile>minimal|enhanced</profile></stellar-wim>`.
  The default is `custom`, so 0.3.x configurations keep their individual flags.
* Runtime profile switching through `StellarProfileMinimal` and
  `StellarProfileEnhanced`, or through `stellar-wimctl profile set`. Switching
  only performs transitions already proven safe: dissolving tab groups,
  updating visibility and recomputing tiling. No renderer, adaptive-sync,
  XWayland or protocol setting is touched.
* IPC additions: top-level `profile` in `VERSION`, `profiles:1` in
  `CAPABILITIES`, `PROFILE GET` / `PROFILE SET`, and profile event frames.
  An individual tiling toggle marks the session `custom`.
* SIGHUP re-applies the configured profile, so runtime changes are intentionally
  not persisted.

### Changed

* Version bumped to 0.4.0.
* Man pages, README and sample `rc.xml` document the profile option and its
  runtime limits.

### Compatibility

* No inherited rc.xml element, action name or Wayland protocol was renamed.
  The XML root remains `<labwc_config>`; profiles are additive and inert unless
  explicitly selected.

### Verification

* `test_stellar_features` adds profile parsing, names, preset application and
  runtime-switch state checks.
* `test_profiles_end_to_end` starts a headless compositor with the `enhanced`
  preset, switches to `minimal` and back, checks `VERSION`, `CAPABILITIES`,
  `tabs status` and `tiling status`, rejects invalid values, verifies a real
  profile event, and confirms SIGHUP re-applies the configured preset.

## 0.3.0 (Fase 3 — optional tabs and tiling)

### Added

* Optional explicit tab groups. `StellarTabGroup`, `StellarTabNext`,
  `StellarTabPrevious` and `StellarTabDetach` operate on groups of mapped
  windows on the same workspace. Only the active tab is visible; inactive tabs
  remain mapped but their scene node is disabled.
* An optional indicator bar for tab groups, built from `wlr_scene_rect` nodes
  and updated only on tab/layout changes. It is only created when tabs are
  enabled and a group exists; there is no polling timer or render loop.
* Optional master/stack tiling with a 3/5 master ratio on each output, derived
  from the usable output area and inherited `rc.gap`. Layout is recomputed only
  on map, unmap, destroy, tab/focus and workspace changes, or on demand.
* Six new actions with a `Stellar` prefix: `StellarTabGroup`, `StellarTabNext`,
  `StellarTabPrevious`, `StellarTabDetach`, `StellarTilingToggle` and
  `StellarTilingReapply`.
* New `<stellar-wim><tabs enabled="..."/></stellar-wim>` and
  `<stellar-wim><tiling enabled="..."/></stellar-wim>` options, both *off* by
  default. They can be changed with SIGHUP; disabling tabs dissolves groups.
* IPC additions without changes to the existing framing, error codes or
  fields: `features.tabs`/`features.tiling`, `capabilities.tabs`/
  `capabilities.tiling`, the `TABS STATUS|LIST|GROUP|NEXT|PREVIOUS|DETACH` and
  `TILING STATUS|REAPPLY` commands, and `tabs`/`tiling` event frames.

### Changed

* Version bumped to 0.3.0.
* `stellar-wimctl` usage text and the config/IPC/action man pages document the
  new options, commands and actions.

### Compatibility

* No inherited rc.xml element, action name or Wayland protocol was renamed.
  The XML root remains `<labwc_config>`; Labwc snap state remains separate from
  the optional Stellar tiling state.

### Verification

* `test_stellar_features` covers tiling box maths (single, two, three and
  2–8 windows without overlap) and tab-group lifecycle/visibility state.
* `test_stellar_features_end_to_end` starts a headless compositor with both
  features enabled and checks feature flags, capabilities, `tabs status`,
  `tabs list`, error paths and `tiling status`/`reapply`.
* Live headless verification with a small real xdg-shell client confirmed
  grouping, switching and detaching two mapped windows, and tiling geometry
  (768+512 = 1280 px on the 1280x720 headless output).

## 0.2.0 (Fase 2 — Stellar IPC)

### Added

* Local IPC server integrated with the wlroots event loop. It uses a Unix
  socket in `XDG_RUNTIME_DIR`, mode 0600, `SO_PEERCRED` uid checking, bounded
  frames and a maximum of 16 clients.
* `stellar-wimctl` command line client with `ping`, `version`, `capabilities`,
  window queries/actions, desktop listing/switching, output listing, effect
  status and event subscription.
* New `<stellar-wim><ipc enabled="..."/></stellar-wim>` option in `rc.xml`.
  IPC is on by default and can be started/stopped with SIGHUP.
* `stellar-wim-ipc(7)` protocol specification, `stellar-wimctl(1)` man page and
  unit/integration tests for the protocol, daemon and CLI.

### Changed

* Version bumped to 0.2.0 to reflect the first functional feature added on top
  of the Fase 1 fork.

### Compatibility

* No inherited rc.xml element, action name or Wayland protocol was renamed.
  The XML root remains `<labwc_config>`.

### Verification

* `test_ipc_protocol` covers framing, malformed headers, tokenization, JSON
  escaping and bounded output.
* `test_ipc_end_to_end` starts the headless compositor and exercises the real
  socket: command responses, invalid commands, event subscription, shutdown
  cleanup, socket mode 0600 and disabled-IPC startup.
* The foreign-uid probe uses `setpriv` when the runner can drop privileges and
  falls back to passwordless `sudo -u nobody`; it is skipped only when neither
  mechanism is available.

## 0.1.0 (Fase 1 — fork funcional)

### Added

* Complete import of the Labwc 0.20.3 source tree as a starting point.
* Independent build identity: Meson project/package/executable `stellar-wim`,
  version `STELLAR_WIM_VERSION`, `LABWC_VERSION` retained as the upstream
  reference.
* Independent installation paths: `stellar-wim.desktop` Wayland session,
  `stellar-wim-portals.conf`, `stellar-wim-session.target`, `stellar-wim*.svg`
  icons, `stellar-wim(1)` and related man pages. Labwc files are not
  overwritten.
* Configuration lookup now prefers `~/.config/stellar-wim` and
  `/etc/xdg/stellar-wim`, falling back to Labwc paths read-only. Theme lookup
  prefers a `stellar-wim/` theme subdirectory before `labwc/` and `openbox-3/`.
* New environment variables `STELLAR_WIM_PID` and `STELLAR_WIM_VER`; inherited
  `LABWC_PID`/`LABWC_VER` remain set for compatibility.
* Project README, this changelog, license notice and a roadmap.

### Changed

* Version output, usage text, help title, fallback icon and Wayland output app
  id now identify Stellar WiM.
* Upstream README/NEWS are preserved under `docs/` with their original
  authorship and license.

### Compatibility

* No upstream C symbol, XDG action name, Wayland protocol or rc.xml element was
  renamed. No functional behavior of Labwc was removed.

### Verification

* `meson setup` + `ninja` succeed on Ubuntu 26.04 with wlroots 0.20.2 built
  from `subprojects/wlroots.wrap`, XWayland and GLES2 renderer enabled.
* Inherited test suite (`meson test`) passes; exact results are recorded in
  `docs/VERIFICATION.md` once the full phase is validated.
