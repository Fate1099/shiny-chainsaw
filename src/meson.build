gnome = import('gnome')

resources = gnome.compile_resources('shinychainsaw-resources',
  '../data/shiny-chainsaw.gresource.xml',
  source_dir: '../data',
  c_name: 'shinychainsaw',
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

executable('shiny-chainsaw',
  app_sources,
  include_directories: include_directories('.'),
  dependencies: [gtkdep, soupdep],
  install: true,
)