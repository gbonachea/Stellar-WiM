scdoc = find_program('scdoc', required: get_option('man-pages'))

if scdoc.found()
  manpages = [
    'stellar-wim.1',
    'stellar-wim-actions.5',
    'stellar-wim-config.5',
    'stellar-wim-menu.5',
    'stellar-wim-theme.5',
    'stellar-wim-philosophy.7',
    'stellar-wim-session.7',
    'stellar-wimctl.1',
    'stellar-wim-ipc.7',
  ]
  foreach manpage : manpages
    markdown = manpage + '.scd'
    custom_target(
      manpage,
      input: markdown,
      output: manpage,
      command: scdoc,
      feed: true,
      capture: true,
      install: true,
      install_dir: join_paths(get_option('mandir'), 'man' + manpage.split('.')[-1])
    )
  endforeach
endif

install_data(
  [
    'autostart',
    'environment',
    'menu.xml',
    'README',
    'VERIFICATION.md',
    'shutdown',
    'themerc',
    'rc.xml',
    'rc.xml.all'
  ],
  install_dir: get_option('datadir') / 'doc' / meson.project_name()
)
