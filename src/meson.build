gnome = import('gnome')

resources = gnome.compile_resources('mdviewer-resources',
  '../data/md-viewer.gresource.xml',
  source_dir: '../data',
  c_name: 'mdviewer',
)

app_sources = files(
  'app.c',
  'main.c',
  'parser.c',
  'recent.c',
  'render.c',
  'shortcuts.c',
) + resources

soupdep = dependency('libsoup-3.0', version: '>= 3.0')

executable('md-viewer',
  app_sources,
  include_directories: include_directories('.'),
  dependencies: [gtkdep, soupdep],
  install: true,
)