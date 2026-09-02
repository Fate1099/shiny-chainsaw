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