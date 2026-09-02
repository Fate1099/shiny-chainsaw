project('md-viewer', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.63.0',
  default_options: [
    'c_std=c11',
    'buildtype=debugoptimized',
    'warning_level=3',
    'werror=false',
  ],
)

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments([
  '-Wall',
  '-Wextra',
  '-Wpedantic',
  '-Wshadow',
  '-Wconversion',
]), language: 'c')

gtkdep = dependency('gtk+-3.0', version: '>= 3.24')

subdir('src')
subdir('tests')