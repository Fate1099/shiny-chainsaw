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
  'render.c',
  'shortcuts.c',
) + resources

executable('md-viewer',
  app_sources,
  include_directories: include_directories('.'),
  dependencies: [gtkdep],
  install: true,
)