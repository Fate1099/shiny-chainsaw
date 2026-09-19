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
  test(name, test_exe)
endforeach

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