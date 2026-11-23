labwc_sources = files(
  'action.c',
  'buffer.c',
  'debug.c',
  'desktop.c',
  'dnd.c',
  'effects-state.c',
  'effects.c',
  'edges.c',
  'idle.c',
  'interactive.c',
  'layers.c',
  'magnifier.c',
  'main.c',
  'node.c',
  'output.c',
  'output-state.c',
  'output-virtual.c',
  'overlay.c',
  'placement.c',
  'profile.c',
  'regions.c',
  'resistance.c',
  'resize-outlines.c',
  'seat.c',
  'server.c',
  'session-lock.c',
  'show-desktop.c',
  'snap-constraints.c',
  'snap.c',
  'tabs.c',
  'tearing.c',
  'theme.c',
  'tiling-layout.c',
  'tiling.c',
  'view.c',
  'view-impl-common.c',
  'window-rules.c',
  'workspaces.c',
  'xdg.c',
  'xdg-popup.c',
)

if have_xwayland
  labwc_sources += files(
    'xwayland.c',
    'xwayland-unmanaged.c',
  )
endif

if have_libsfdo
  labwc_sources += files(
    'desktop-entry.c',
  )
endif

subdir('common')
subdir('config')
subdir('cycle')
subdir('ipc')
subdir('decorations')
subdir('foreign-toplevel')
subdir('img')
subdir('input')
subdir('menu')
subdir('scaled-buffer')
subdir('ssd')
