# Stellar WiM development roadmap

Features are only marked done after they compile and have automated tests or
documented manual verification.

| Phase | Scope | State |
| --- | --- | --- |
| 1 | Functional fork: identity, config paths, Meson, build and inherited tests | Implemented and verified |
| 2 | Stellar IPC socket + `stellar-wimctl`, security and tests | Implemented and verified |
| 3 | Optional tabbed groups and tiling (floating stays the default) | Implemented and verified |
| 4 | `minimal` / `enhanced` profiles, runtime switching where safe | Implemented and verified |
| 5 | Optional animations/shadows/opacity with no idle render loop | Implemented and verified |
| 6 | Packaging, session integration, performance comparison | Implemented and verified |
| 7 | 0.8.0 completion batch: fade-out, arbitrary tab groups, tiling modes/rules, profile effects and install isolation | Implemented and verified |

## 0.8.0 completion batch

* Fade-out on minimize and tab hide with deferred scene visibility. Blur,
  workspace-switch animation and geometry interpolation are explicitly out of
  scope for this batch (wlroots 0.20 has no scene blur API).
* Tab groups accept 2..32 window ids over IPC, can extend existing groups,
  have clickable bar segments and share one geometry among all members.
* Tiling offers five modes with configurable ratio, columns and size limits,
  per-window `stellarTilingMode` rules, `TILING SET` and an enriched status.
* Profiles also toggle effects: `minimal` disables animations/opacity/shadows,
  `enhanced` restores them. Effects remain on by default; tabs/tiling off.
* Meson strips build-only subproject install destinations for every install,
  not just for Debian packages.

Design constraints:

* No new mandatory process or heavy dependency.
* No render loop, timer or GPU resource for a disabled feature; a fade-out may
  defer visibility, but must leave no timer or enabled hidden view once done.
* Preserve Labwc's public protocols, action names and rc.xml elements.
* Keep IPC bounded and non-blocking: `STELLAR_IPC_MAX_TOKENS` (40) allows
  `GROUP` with 2..32 ids; a conservative non-blocking socket probe never
  unlinks a socket that might still be live.
* The compositor must remain installable side by side with Labwc.
