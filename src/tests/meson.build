executable(
  'ethernet-dialog',
  [resource_sources,
    '../applet-dialogs.c',
    '../applet-dialogs.h',
    '../ethernet-dialog.c',
    '../ethernet-dialog.h',
    'ethernet-dialog.c'],
  include_directories: incs,
  dependencies: deps,
  c_args: cflags,
  link_whole: libwireless_security_libnm,
  install: false
)
