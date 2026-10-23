test_dep = dependency('glib-2.0')

tests = {
  'test_parser': files('test_parser.c'),
}

foreach name, sources : tests
  test_exe = executable(
    name,
    sources: [sources, '../src/parser.c'],
    include_directories: include_directories('../src'),
    dependencies: [test_dep],
  )
  test(name, test_exe, suite: 'unit')
endforeach

robustness_test = executable(
  'test_robustness',
  sources: ['test_robustness.c', '../src/parser.c'],
  include_directories: include_directories('../src'),
  dependencies: [test_dep],
)

test('robustness', robustness_test, suite: 'robustness')

spec_tests = executable(
  'spec_tests',
  sources: ['spec_tests.c', '../src/parser.c'],
  include_directories: include_directories('../src'),
  dependencies: [test_dep],
)

commonmark_spec = join_paths(meson.current_source_dir(), 'spec', 'commonmark.txt')
gfm_spec = join_paths(meson.current_source_dir(), 'spec', 'gfm.txt')

test('commonmark-spec', spec_tests, args: [commonmark_spec, 'commonmark'], suite: 'conformance')
test('gfm-spec', spec_tests, args: [gfm_spec, 'gfm'], suite: 'conformance')

render_test = executable(
  'test_render',
  sources: ['test_render.c', '../src/parser.c', '../src/render.c'],
  include_directories: include_directories('../src'),
  dependencies: [test_dep, gtkdep, dependency('libsoup-3.0', version: '>= 3.0')],
)

test('render', render_test, suite: 'render')