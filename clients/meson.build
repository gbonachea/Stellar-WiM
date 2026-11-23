# Stellar IPC command line client. It only needs libc and the shared,
# dependency-free protocol encoder/decoder.
stellar_wimctl_exe = executable(
  'stellar-wimctl',
  files(
    'stellar-wimctl.c',
    '../src/ipc/protocol.c',
  ),
  include_directories: [labwc_inc],
  install: true,
)

if get_option('labnag').allowed()
  wayland_client = dependency('wayland-client')
  wayland_cursor = dependency('wayland-cursor')

  nag_sources = files(
    'labnag.c',
    'pool-buffer.c',
  )

  wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

  protocols = [
    wl_protocol_dir / 'stable/tablet/tablet-v2.xml',
    wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
    wl_protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
    '../protocols/wlr-layer-shell-unstable-v1.xml',
  ]

  foreach xml : protocols
    nag_sources += custom_target(
      xml.underscorify() + '_c',
      input: xml,
      output: '@BASENAME@-protocol.c',
      command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    nag_sources += custom_target(
      xml.underscorify() + '_client_h',
      input: xml,
      output: '@BASENAME@-client-protocol.h',
      command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
  endforeach

  if host_machine.system() in ['freebsd', 'openbsd']
    # For signalfd()
    epoll_dep = dependency('epoll-shim')
  else
    epoll_dep = []
  endif

  executable(
    'labnag',
    nag_sources,
    dependencies: [
      cairo,
      pangocairo,
      glib,
      wayland_client,
      wayland_cursor,
      wlroots,
      server_protos,
      epoll_dep,
      xkbcommon,
    ],
    include_directories: [labwc_inc],
    # Kept for source parity with Labwc, but not shipped by the fork.
    install: false,
  )
endif

# Labwc's generic terminal wrapper is not part of the Stellar WiM install tree.
