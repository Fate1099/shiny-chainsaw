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

# Lint target (formatting + static analysis). Requires clang-format and/or
# clang-tidy to be installed; skips whichever tool is missing.
run_target('lint',
  command: [find_program('sh'), files('scripts/lint.sh')])

install_data('data/md-viewer.desktop',
  install_dir: get_option('datadir') / 'applications')
install_data('data/icons/md-viewer.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
install_data('data/org.example.mdviewer.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas')

subdir('src')
subdir('tests')