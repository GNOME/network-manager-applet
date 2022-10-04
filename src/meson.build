src_inc = include_directories('.')

subdir('utils')
subdir('wireless-security')

subdir('connection-editor')

sources = files(
  'ap-menu-item.c',
  'applet-agent.c',
  'applet.c',
  'applet-device-bt.c',
  'applet-device-ethernet.c',
  'applet-device-wifi.c',
  'applet-dialogs.c',
  'applet-vpn-request.c',
  'ethernet-dialog.c',
  'main.c',
  'mb-menu-item.c',
  'mobile-helpers.c'
)

resource_data = files(
  '8021x.ui',
  'connection-editor/ce-page-dsl.ui',
  'gsm-unlock.ui',
  'info.ui'
)

resource_sources = gnome.compile_resources(
  'applet-resources',
  'applet.gresource.xml',
  dependencies: resource_data
)

sources += resource_sources

incs = [
  top_inc,
  utils_inc,
  src_inc,
  shared_inc,
  wireless_security_inc
]

deps = [
  gtk_dep,
  libnm_dep,
  libnma_dep,
  libsecret_dep,
  m_dep,
  libutils_libnm_dep
]

cflags = [
  '-DBINDIR="@0@"'.format(nma_bindir),
  '-DDATADIR="@0@"'.format(nma_datadir),
  '-DLIBDIR="@0@"'.format(nma_libdir),
  '-DLIBEXECDIR="@0@"'.format(nma_libexecdir),
  '-DNMALOCALEDIR="@0@"'.format(nma_localedir),
  '-DSYSCONFDIR="@0@"'.format(nma_sysconfdir),
  '-DAUTOSTARTDIR="@0@"'.format(nma_autostartdir),
  '-DICONDIR="@0@"'.format(nma_icondir),
  '-DG_LOG_DOMAIN="@0@"'.format(nma_name)
]

if enable_appindicator == 'ayatana' or enable_appindicator == 'ubuntu'
  deps += [
    appindicator_dep,
    dbusmenu_dep
  ]
endif

if enable_wwan
  sources += files('applet-device-broadband.c')

  deps += mm_glib_dep
endif

executable(
  nma_name,
  sources,
  include_directories: incs,
  dependencies: deps,
  c_args: cflags,
  link_args: '-Wl,--version-script,@0@'.format(linker_script_ver),
  link_depends: linker_script_ver,
  link_whole: libwireless_security_libnm,
  install: true,
  install_dir: nma_bindir
)
