test_deps = [
  dep_cmocka,
  input,
  glib,
  xml2,
  wlroots,
]

test_lib = static_library(
  'test_lib',
  sources: files(
    '../src/config/accel.c',
    '../src/common/parse-double.c',
    '../src/common/buf.c',
    '../src/common/mem.c',
    '../src/common/string-helpers.c',
    '../src/common/xml.c',
    '../src/common/parse-bool.c',
  ),
  include_directories: [labwc_inc],
  dependencies: test_deps,
)

tests = [
  'accel',
  'buf-simple',
  'str',
  'xml',
]

foreach t : tests
  test(
    'test_@0@'.format(t),
    executable(
      'test_@0@'.format(t),
      sources: '@0@.c'.format(t),
      include_directories: [labwc_inc],
      link_with: [test_lib],
      dependencies: test_deps,
    ),
    is_parallel: false,
  )
endforeach

test(
  'test_ipc_protocol',
  executable(
    'test_ipc_protocol',
    sources: files('ipc.c', '../src/ipc/protocol.c'),
    include_directories: [labwc_inc],
    dependencies: [dep_cmocka],
  ),
  is_parallel: false,
)

test(
  'test_effects',
  executable(
    'test_effects',
    sources: files('effects.c', '../src/effects-state.c'),
    include_directories: [labwc_inc],
    dependencies: [dep_cmocka],
  ),
  is_parallel: false,
)

test(
  'test_stellar_features',
  executable(
    'test_stellar_features',
    sources: files(
      'stellar-features.c',
      '../src/profile.c',
      '../src/tabs.c',
      '../src/tiling-layout.c',
    ),
    include_directories: [labwc_inc],
    dependencies: test_deps + [cairo, pangocairo],
  ),
  is_parallel: false,
)
