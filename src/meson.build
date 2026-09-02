app_sources = files(
  'app.c',
  'main.c',
  'parser.c',
)

executable('md-viewer',
  app_sources,
  include_directories: include_directories('.'),
  dependencies: [gtkdep],
  install: true,
)