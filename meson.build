project(
  'network-manager-applet', 'c',
  version: '1.37.0',
  license: 'GPL2+',
  default_options: [
    'buildtype=debugoptimized',
    'c_std=gnu99'
  ],
  meson_version: '>= 0.46.0'
)

nma_name = 'nm-applet'

nm_applet_version = meson.project_version()
version_array = nm_applet_version.split('.')
nm_applet_major_version = version_array[0].to_int()
nm_applet_minor_version = version_array[1].to_int()
nm_applet_micro_version = version_array[2].to_int()

nma_gir_version = '1.0'
nma_gir_prefix = 'NMA'

nma_prefix = get_option('prefix')
nma_bindir = join_paths(nma_prefix, get_option('bindir'))
nma_datadir = join_paths(nma_prefix, get_option('datadir'))
nma_includedir = join_paths(nma_prefix, get_option('includedir'))
nma_libdir = join_paths(nma_prefix, get_option('libdir'))
nma_libexecdir = join_paths(nma_prefix, get_option('libexecdir'))
nma_localedir = join_paths(nma_prefix, get_option('localedir'))
nma_mandir = join_paths(nma_prefix, get_option('mandir'))
nma_sysconfdir = join_paths(nma_prefix, get_option('sysconfdir'))

nma_appdir = join_paths(nma_datadir, 'applications')
nma_autostartdir = join_paths(nma_sysconfdir, 'xdg', 'autostart')
nma_icondir = join_paths(nma_datadir, 'icons')

soversion = 0
current = 0
revision = 0
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)

nma_debug = get_option('buildtype').contains('debug')

cc = meson.get_compiler('c')

config_h = configuration_data()

# defines
config_h.set_quoted('GETTEXT_PACKAGE', nma_name)
config_h.set_quoted('VERSION', nm_applet_version)

# types
if not cc.has_type('pid_t', prefix: '#include<sys/types.h>')
  config_h.set('pid_t', 'int')
endif

# compiler flags
common_flags = []
common_ldflags = []

enable_ld_gc = get_option('ld_gc')
if enable_ld_gc
  test_cflags = [
    '-fdata-sections',
    '-ffunction-sections',
  ]

  test_ldflags = ['-Wl,--gc-sections']

  assert(cc.has_multi_arguments(test_cflags), 'Unused symbol eviction requested but not supported. Use -Dld_gc=false to build without it.')
  assert(cc.has_multi_link_arguments(test_ldflags), 'Unused symbol eviction requested but not supported. Use -Dld_gc=false to build without it.')

  common_flags += test_cflags
  common_ldflags += test_ldflags
endif

if nma_debug
  test_cflags = [
    '-Wdeclaration-after-statement',
    '-Wformat=2',
    '-Wimplicit-fallthrough',
    '-Winit-self',
    '-Wmissing-declarations',
    '-Wmissing-include-dirs',
    '-Wmissing-prototypes',
    '-Wno-duplicate-decl-specifier',
    '-Wno-format-truncation',
    '-Wno-format-y2k',
    '-Wno-missing-field-initializers',
    '-Wno-pragmas',
    '-Wno-sign-compare',
    '-Wno-unused-parameter',
    '-Wshadow',
    '-Wstrict-prototypes',
    '-Wundef',
    '-Wvla',
  ]

  common_flags += cc.get_supported_arguments(test_cflags)
endif

add_project_arguments(common_flags, language: 'c')
add_project_link_arguments(common_ldflags, language: 'c')

linker_script_ver = join_paths(meson.source_root(), 'linker-script-binary.ver')

gio_dep = dependency('gio-2.0', version: '>= 2.40')
gmodule_export_dep = dependency('gmodule-export-2.0')
libsecret_dep = dependency('libsecret-1', version: '>= 0.18')
libnma_dep = dependency('libnma', version: '>= 1.8.27')

m_dep = cc.find_library('m')

# Check for gtk+
gtk_req_version = '>= 3.10'

gtk_dep = declare_dependency(
  dependencies: dependency('gtk+-3.0', version: gtk_req_version),
  compile_args: [
    '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_10',
    '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_10'
  ]
)

# API documentation
nm_req_version = '>= 1.7'

deps = [
  gio_dep,
  gmodule_export_dep,
  dependency('libnm', version: nm_req_version)
]

cflags = [
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_40',
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_40',
  '-DNM_VERSION_MIN_REQUIRED=NM_VERSION_1_16',
  '-DNM_VERSION_MAX_ALLOWED=NM_VERSION_1_16',
  '-DNMA_VERSION_MIN_REQUIRED=NMA_VERSION_1_8_28',
]

libnm_dep = declare_dependency(
  dependencies: deps,
  compile_args: cflags
)

# No such thing yet, just same lame duck placeholders.
# it may actually end up being called differently.
config_h.set10('WITH_GCR_GTK4', false)

enable_appindicator = get_option('appindicator')
if enable_appindicator == 'auto' or enable_appindicator == 'yes'
  appindicator_dep = dependency('ayatana-appindicator3-0.1', required: false)
  if appindicator_dep.found()
    enable_appindicator = 'ayatana'
  else
    appindicator_dep = dependency('appindicator3-0.1', required: false)
    if appindicator_dep.found()
      enable_appindicator = 'ubuntu'
    else
      assert(appindicator_dep.found(), 'Neither Ubuntu\'s AppIndicator nor Ayatana AppIndicator found.')
    endif
  endif
elif enable_appindicator == 'ayatana'
  appindicator_dep = dependency('ayatana-appindicator3-0.1')
elif enable_appindicator == 'ubuntu'
  appindicator_dep = dependency('appindicator3-0.1')
endif

if enable_appindicator == 'no'
  config_h.set('WITH_APPINDICATOR', false)
  config_h.set('USE_AYATANA_INDICATORS', false)
elif enable_appindicator == 'ubuntu'
  dbusmenu_dep = dependency('dbusmenu-gtk3-0.4', version: '>= 16.04.0')
  config_h.set10('WITH_APPINDICATOR', true)
  config_h.set10('USE_AYATANA_INDICATORS', false)
elif enable_appindicator == 'ayatana'
  dbusmenu_dep = dependency('dbusmenu-gtk3-0.4', version: '>= 16.04.0')
  config_h.set10('WITH_APPINDICATOR', true)
  config_h.set10('USE_AYATANA_INDICATORS', true)
else
  error('Options allowed for -Dappindicator=<str> are: no, yes|auto, ayatana, ubuntu.')
endif

# ModemManager1 with libmm-glib for WWAN support
enable_wwan = get_option('wwan')
if enable_wwan
  mm_glib_dep = dependency('mm-glib', required: false)
  assert(mm_glib_dep.found(), 'libmm-glib is needed for WWAN support. Use -Dwwan=false to build without it.')
endif
config_h.set10('WITH_WWAN', enable_wwan)

# SELinux
enable_selinux = get_option('selinux')
if enable_selinux
  libselinux_dep = dependency('libselinux', required: false)
  assert(libselinux_dep.found(), 'libselinux is needed for SELinux label support in configuration editor. Use -Dselinux=false to build without it.')
endif
config_h.set10('WITH_SELINUX', enable_selinux)

# Jansson for team configuration editing
enable_team = get_option('team')
if enable_team
  jansson_dep = dependency('jansson', version: '>= 2.7', required: false)
  assert(jansson_dep.found(), 'jansson is needed for team configuration editor. Use -Dteam=false to build without it.')
endif
config_h.set10('WITH_JANSSON', enable_team)

more_asserts = get_option('more_asserts')
if more_asserts == 'no'
  more_asserts = 0
elif more_asserts == 'yes'
  more_asserts = 100
else
  more_asserts = more_asserts.to_int()
endif
config_h.set('NM_MORE_ASSERTS', more_asserts)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

po_dir = join_paths(meson.source_root(), 'po')

top_inc = include_directories('.')

subdir('po')
subdir('icons')
subdir('shared')
subdir('src')
subdir('src/tests')
subdir('man')

i18n = import('i18n')

# FIXME: The same target can not be copied into two directories.
#        There is a workaround in meson_post_install.py until proper solution arises:
#        https://groups.google.com/forum/#!topic/mesonbuild/3iIoYPrN4P0
desktop_files = [
  'nm-applet',
  'nm-connection-editor',
]

desktop_file_validate = find_program('desktop-file-validate', required: false)
desktop_file_install = find_program('desktop-file-install', required: false)

foreach desktop: desktop_files
  i18n.merge_file(
    input: desktop + '.desktop.in',
    output: desktop + '.desktop',
    install: true,
    install_dir: nma_appdir,
    po_dir: po_dir,
    type: 'desktop',
  )

  if desktop_file_install.found()
    # desktop-file-install also does validation, post-editing.
    # Used that to force tolerating COSMIC in NoShowIn.
    #
    # This can be removed once desktop-file-utils with support for COSMIC
    # (desktop-file-utils.git commit 67d09f0c851c 'validate: add COSMIC')
    # is released. Not the case as of Oct 14 2024 (Fedora 41).
    test('validate-' + desktop + '-desktop',
      desktop_file_install,
      args: [
        '--remove-not-show-in', 'COSMIC',
        '--vendor', 'test',
        '--dir',  meson.current_build_dir(),
        desktop + '.desktop',
      ],
    )
  elif desktop_file_validate.found()
    test('validate-' + desktop + '-desktop',
      desktop_file_validate,
      args: desktop + '.desktop',
      workdir: meson.current_build_dir(),
    )
  endif
endforeach

metainfo = 'org.gnome.nm-connection-editor.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  install: true,
  install_dir: join_paths(nma_datadir, 'metainfo'),
  po_dir: po_dir,
)

appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test('validate-metainfo',
    appstreamcli,
    args: ['validate', '--no-net', '--explain', metainfo],
    workdir: meson.current_build_dir(),
  )
endif

schema_conf = configuration_data()
schema_conf.set('GETTEXT_PACKAGE', nma_name)

schema = 'org.gnome.nm-applet.gschema.xml'

configure_file(
  input: schema + '.in',
  output: schema,
  install_dir: join_paths(nma_datadir, 'glib-2.0', 'schemas'),
  configuration: schema_conf
)

install_data(
  'nm-applet.convert',
  install_dir: join_paths(nma_datadir, 'GConf', 'gsettings')
)

config = 'config.h'

configure_file(
  input: config + '.meson',
  output: config,
  configuration: config_h
)

meson.add_install_script(
  'meson_post_install.py',
  nma_datadir,
  nma_sysconfdir
)

output = '\n'
output += '  LTO: ' + get_option('b_lto').to_string() + '\n'
output += '  Linker garbage collection: ' + enable_ld_gc.to_string() + '\n'
output += '  libappindicator: ' + enable_appindicator
message(output)
