src_inc = include_directories('.')

version_conf = configuration_data()
version_conf.set('NMA_MAJOR_VERSION', nma_major_version)
version_conf.set('NMA_MINOR_VERSION', nma_minor_version)
version_conf.set('NMA_MICRO_VERSION', nma_micro_version)

version = 'nma-version.h'

version_header = configure_file(
  input: version + '.in',
  output: version,
  configuration: version_conf,
  install_dir: join_paths(nma_includedir, 'libnma')
)

subdir('utils')
subdir('wireless-security')

cert_chooser_headers = files('nma-cert-chooser.h')

cert_chooser_sources = files(
  'nma-cert-chooser.c',
  'nma-file-cert-chooser.c'
)

resource_data = files(
  'nma-pkcs11-cert-chooser-dialog.ui',
  'nma-pkcs11-token-login-dialog.ui',
  'wifi.ui'
)

built_sources = gnome.compile_resources(
  'nma-resources',
  'nma.gresource.xml',
  dependencies: resource_data
)

gir_headers = cert_chooser_headers + files(
  'nma-bar-code-widget.h',
  'nma-bar-code.h',
  'nma-mobile-providers.h',
  'nma-mobile-wizard.h',
  'nma-ui-utils.h',
  'nma-vpn-password-dialog.h',
  'nma-wifi-dialog.h'
)

install_headers(
  gir_headers,
  subdir: 'libnma'
)

gir_sources = [version_header] + cert_chooser_sources + files(
  'init.c',
  'nma-bar-code-widget.c',
  'nma-bar-code.c',
  'nma-mobile-providers.c',
  'nma-mobile-wizard.c',
  'nma-ui-utils.c',
  'nma-vpn-password-dialog.c',
  'nma-wifi-dialog.c'
)

incs = [
  top_inc,
  shared_inc,
  src_inc,
  wireless_security_inc
]

deps = [
  libnm_dep,
  libutils_libnm_dep,
]

cflags = [
  '-DNETWORKMANAGER_COMPILATION=NM_NETWORKMANAGER_COMPILATION_LIB',
  '-DBINDIR="@0@"'.format(nma_bindir),
  '-DDATADIR="@0@"'.format(nma_datadir),
  '-DLIBEXECDIR="@0@"'.format(nma_libexecdir),
  '-DNMALOCALEDIR="@0@"'.format(nma_localedir),
  '-DSYSCONFDIR="@0@"'.format(nma_sysconfdir),
  '-DAUTOSTARTDIR="@0@"'.format(nma_autostartdir),
  '-DICONDIR="@0@"'.format(nma_icondir)
]

symbol_map = join_paths(meson.current_source_dir(), 'libnma.ver')

nma_deps = [ deps, gtk_dep ]
nma_sources = [ gir_sources, built_sources ]
if enable_gcr
  nma_sources += files(
    'nma-cert-chooser-button.c',
    'nma-pkcs11-cert-chooser.c',
    'nma-pkcs11-cert-chooser-dialog.c',
    'nma-pkcs11-token-login-dialog.c'
  )
  nma_deps += gcr_dep
endif

libnma = shared_library(
  'nma',
  sources: nma_sources,
  version: libversion,
  include_directories: incs,
  dependencies: nma_deps,
  c_args: cflags,
  link_args: '-Wl,--version-script,' + symbol_map,
  link_depends: symbol_map,
  link_whole: libwireless_security_libnm,
  install: true,
  install_dir: nma_libdir
)

libnma_dep = declare_dependency(
  link_with: libnma,
  include_directories: src_inc
)

if enable_libnma_gtk4
  libnma_gtk4 = shared_library(
    'nma-gtk4',
    sources: gir_sources + built_sources,
    version: libversion,
    include_directories: incs,
    dependencies: [ deps, gtk4_dep ],
    c_args: cflags,
    link_args: '-Wl,--version-script,' + symbol_map,
    link_depends: symbol_map,
    link_whole: libwireless_security_libnma_gtk4,
    install: true,
    install_dir: nma_libdir
  )

  libnma_gtk4_dep = declare_dependency(
    link_with: libnma_gtk4,
    include_directories: src_inc
  )
endif

subdir('tests')

pkg.generate(
  libraries: libnma,
  version: nma_version,
  name: 'libnma',
  description: 'NetworkManager UI utilities (libnm version)',
  filebase: 'libnma',
  subdirs: 'libnma',
  requires: 'libnm',
  variables: 'exec_prefix=' + nma_prefix,
  install_dir: join_paths(nma_libdir, 'pkgconfig')
)

if enable_libnma_gtk4
  pkg.generate(
    libraries: libnma_gtk4,
    version: nma_version,
    name: 'libnma-gtk4',
    description: 'NetworkManager UI utilities (gtk4 version)',
    filebase: 'libnma-gtk4',
    subdirs: 'libnma',
    requires: 'libnm',
    variables: 'exec_prefix=' + nma_prefix,
    install_dir: join_paths(nma_libdir, 'pkgconfig')
  )
endif

if enable_introspection
  gir_dir = join_paths(nma_datadir, 'gir-' + nma_gir_version)
  typelib_dir = join_paths(nma_libdir, 'girepository-' + nma_gir_version)

  gnome.generate_gir(
    libnma,
    sources: gir_sources + gir_headers,
    nsversion: nma_gir_version,
    namespace: 'NMA',
    includes: [ 'Gtk-3.0', 'NM-1.0' ],
    identifier_prefix: nma_gir_prefix,
    symbol_prefix: nma_gir_prefix.to_lower(),
    export_packages: 'libnma',
    install: true,
    install_dir_gir: gir_dir,
    install_dir_typelib: typelib_dir
  )

  if enable_libnma_gtk4
    gnome.generate_gir(
      libnma,
      sources: gir_sources + gir_headers,
      nsversion: nma_gir_version,
      namespace: 'NMA4',
      includes: [ 'Gtk-4.0', 'NM-1.0' ],
      identifier_prefix: nma_gir_prefix,
      symbol_prefix: nma_gir_prefix.to_lower(),
      export_packages: 'libnma',
      install: true,
      install_dir_gir: gir_dir,
      install_dir_typelib: typelib_dir
    )
  endif
endif
