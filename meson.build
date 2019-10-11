project(
  'libnma', 'c',
  version: '1.8.25',
  license: 'GPL2+',
  default_options: [
    'buildtype=debugoptimized',
    'c_std=gnu99'
  ],
  meson_version: '>= 0.46.0'
)

nma_version = meson.project_version()
version_array = nma_version.split('.')
nma_major_version = version_array[0].to_int()
nma_minor_version = version_array[1].to_int()
nma_micro_version = version_array[2].to_int()

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
config_h.set_quoted('GETTEXT_PACKAGE', 'libnma')
config_h.set_quoted('VERSION', nma_version)

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

enable_libnma_gtk4 = get_option('libnma_gtk4')
if enable_libnma_gtk4
  gtk4_ext_dep = dependency('gtk4', version: '>= 3.96')
  gtk4_dep = declare_dependency(
    dependencies: gtk4_ext_dep,
    compile_args: [
      '-DGTK_VERSION_MIN_REQUIRED=GTK_VERSION_3_96',
      '-DGTK_VERSION_MAX_ALLOWED=GTK_VERSION_3_96'
    ]
  )
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

  # Disable extra compiler warning until GTK4 doesn't trigger it via graphene
  # https://github.com/ebassi/graphene/issues/134
  if not enable_libnma_gtk4 or cc.compiles('#include <gtk/gtk.h>', dependencies: gtk4_ext_dep, args: ['-Werror=float-equal'])
    test_cflags += '-Wfloat-equal'
  endif

  common_flags += cc.get_supported_arguments(test_cflags)
endif

add_project_arguments(common_flags, language: 'c')
add_project_link_arguments(common_ldflags, language: 'c')

linker_script_ver = join_paths(meson.source_root(), 'linker-script-binary.ver')

# Check for iso-codes for country names translation
enable_iso_codes = get_option('iso_codes')
message('whether to disable iso-codes at build-time: ' + enable_iso_codes.to_string())
if enable_iso_codes
  iso_codes_dep = dependency('iso-codes')

  str = 'Consider installing the package or adjusting the PKG_CONFIG_PATH environment variable.\n'
  str += 'You can also disable build-time check for \'iso-codes\' via -Diso_codes=false'
  assert(iso_codes_dep.found(), str)

  iso_3166 = iso_codes_dep.get_pkgconfig_variable('domains').contains('iso_3166')
  message('whether iso-codes has iso_3166 domain: ' + iso_3166.to_string())

  config_h.set_quoted('ISO_CODES_PREFIX', iso_codes_dep.get_pkgconfig_variable('prefix'))
else
  config_h.set_quoted('ISO_CODES_PREFIX', nma_prefix)
endif

# Check for mobile-broadband-provider-info for Mobile Broadband wizard
enable_mobile_broadband_provider_info = get_option('mobile_broadband_provider_info')
message('whether to enable mobile-broadband-provider-info at build-time: ' + enable_mobile_broadband_provider_info.to_string())
if enable_mobile_broadband_provider_info
  mobile_broadband_provider_info_dep = dependency('mobile-broadband-provider-info')
  config_h.set_quoted('MOBILE_BROADBAND_PROVIDER_INFO_DATABASE', mobile_broadband_provider_info_dep.get_pkgconfig_variable('database'))
else
  config_h.set_quoted('MOBILE_BROADBAND_PROVIDER_INFO_DATABASE', join_paths(nma_datadir, 'mobile-broadband-provider-info', 'serviceproviders.xml'))
endif

gio_dep = dependency('gio-2.0', version: '>= 2.38')
gmodule_export_dep = dependency('gmodule-export-2.0')

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
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_38',
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_38',
  '-DNM_VERSION_MIN_REQUIRED=NM_VERSION_1_8',
  '-DNM_VERSION_MAX_ALLOWED=NM_VERSION_1_8'
]

libnm_dep = declare_dependency(
  dependencies: deps,
  compile_args: cflags
)

# No such thing yet, just same lame duck placeholders.
# it may actually end up being called differently.
config_h.set10('WITH_GCR_GTK4', false)

# GCR for PKCS#11 enabled certificate chooser
enable_gcr = get_option('gcr')
if enable_gcr
  deps = [
    dependency('gcr-3', version: '>= 3.14'),
    dependency('gck-1', version: '>= 3.14')
  ]

  cflags = [
    '-DGCR_API_SUBJECT_TO_CHANGE',
    '-DGCK_API_SUBJECT_TO_CHANGE'
  ]

  # GCR API is declared subject to change, do an extensive check of the prototypes
  gcr_src = '''
    #ifndef GCR_API_SUBJECT_TO_CHANGE
    # define GCR_API_SUBJECT_TO_CHANGE
    #endif
    #ifndef GCK_API_SUBJECT_TO_CHANGE
    # define GCK_API_SUBJECT_TO_CHANGE
    #endif

    #include <gcr/gcr.h>
    #include <gck/gck.h>

    const GckAttribute *gck_attributes_find (GckAttributes *attrs, gulong attr_type);
    gboolean gck_attributes_find_string (GckAttributes *attrs, gulong attr_type, gchar **value);
    gboolean gck_attributes_find_ulong (GckAttributes *attrs, gulong attr_type, gulong *value);
    GckAttributes *gck_attributes_new_empty (gulong first_type, ...);
    void gck_attributes_unref (gpointer attrs);
    void gck_builder_add_all (GckBuilder *builder, GckAttributes *attrs);
    void gck_builder_add_only (GckBuilder *builder, GckAttributes *attrs, gulong only_type, ...);
    GckAttributes *gck_builder_end (GckBuilder *builder);
    GckBuilder *gck_builder_new (GckBuilderFlags flags);
    void gck_enumerator_next_async (GckEnumerator *self, gint max_objects, GCancellable *cancellable,
                                    GAsyncReadyCallback callback, gpointer user_data);
    GList *gck_enumerator_next_finish (GckEnumerator *self, GAsyncResult *result, GError **error);
    void gck_list_unref_free (GList *reflist);
    GList *gck_modules_get_slots (GList *modules, gboolean token_present);
    void gck_modules_initialize_registered_async (GCancellable *cancellable, GAsyncReadyCallback callback,
                                                  gpointer user_data);
    GList *gck_modules_initialize_registered_finish (GAsyncResult *result, GError **error);
    void gck_object_get_async (GckObject *self, const gulong *attr_types, guint n_attr_types,
                               GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
    GckAttributes *gck_object_get_finish (GckObject *self, GAsyncResult *result, GError **error);
    void gck_session_login_async (GckSession *self, gulong user_type, const guchar *pin,
                                  gsize n_pin, GCancellable *cancellable,
                                  GAsyncReadyCallback callback, gpointer user_data);
    gboolean gck_session_login_finish (GckSession *self, GAsyncResult *result, GError **error);
    GckSlotInfo *gck_slot_get_info (GckSlot *self);
    GckTokenInfo *gck_slot_get_token_info (GckSlot *self);
    void gck_slot_open_session_async (GckSlot *self, GckSessionOptions options, GCancellable *cancellable,
                                      GAsyncReadyCallback callback, gpointer user_data);
    GckSession *gck_slot_open_session_finish (GckSlot *self, GAsyncResult *result, GError **error);
    void gck_token_info_free (GckTokenInfo *token_info);
    gchar *gck_uri_build (GckUriData *uri_data, GckUriFlags flags);
    void gck_uri_data_free (GckUriData *uri_data);
    GckUriData *gck_uri_parse (const gchar *string, GckUriFlags flags, GError **error);
    gchar *gcr_certificate_get_issuer_name (GcrCertificate *self);
    gchar *gcr_certificate_get_subject_name (GcrCertificate *self);
    GcrCertificate *gcr_simple_certificate_new (const guchar *data, gsize n_data);
  '''

  assert(cc.compiles(gcr_src, dependencies: deps), 'gcr support was requested, but the gcr library is not available. Use -Dgcr=false to build without it.')

  gcr_dep = declare_dependency(
    dependencies: deps,
    compile_args: cflags
  )
endif
config_h.set10('WITH_GCR', enable_gcr)

# introspection support
enable_introspection = get_option('introspection')
if enable_introspection
  gir_dep = dependency('gobject-introspection-1.0', version: '>= 0.9.6', required: false)
  assert(gir_dep.found(), 'introspection support was requested, but the gobject-introspection library is not available. Use -Dintrospection=false to build without it.')
endif

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
subdir('shared')
subdir('src')

# documentation
enable_gtk_doc = get_option('gtk_doc')
if enable_gtk_doc
  mod_name = 'libnma'

  private_headers = [
    'nma-cert-chooser-button.h',
    'nma-cert-chooser-private.h',
    'nma-pkcs11-cert-chooser-dialog.h',
    'nma-pkcs11-token-login-dialog.h',
    'nma-resources.h',
    'nma-private.h',
    'nma-version.h',
    'eap-method.h',
    'wireless-security.h',
  ]

  private_sources = [
    'nma-cert-chooser-button.c',
    'nma-file-cert-chooser.c',
    'nma-pkcs11-cert-chooser-dialog.c',
    'nma-pkcs11-cert-chooser.c',
    'nma-pkcs11-token-login-dialog.c',
    'nma-resources.c'
  ]

  ent_conf = configuration_data()
  ent_conf.set_quoted('PACKAGE', meson.project_name())
  ent_conf.set_quoted('PACKAGE_BUGREPORT', 'https://gitlab.gnome.org/GNOME/libnma/issues')
  ent_conf.set_quoted('PACKAGE_NAME', 'libnma')
  ent_conf.set_quoted('PACKAGE_STRING', 'libnma @0@'.format(nma_version))
  ent_conf.set_quoted('PACKAGE_TARNAME', meson.project_name())
  ent_conf.set_quoted('PACKAGE_URL', '')
  ent_conf.set_quoted('PACKAGE_VERSION', nma_version)

  ent = 'gtkdocentities.ent'

  configure_file(
    input: ent + '.in',
    output: ent,
    configuration: ent_conf
  )

  doc_path = join_paths(nma_datadir, 'gtk-doc', 'html', mod_name)

  gnome.gtkdoc(
    mod_name,
    main_xml: mod_name + '-docs.xml',
    src_dir: src_inc,
    dependencies: libnma_dep,
    scan_args: [
      '--rebuild-types',
      '--rebuild-sections',
      '--ignore-headers=' + ' '.join(private_headers)
    ],
    mkdb_args: '--ignore-files=' + ' '.join(private_headers + private_sources),
    gobject_typesfile: mod_name + '.types',
    install: true,
    install_dir: doc_path
  )
endif

config = 'config.h'

configure_file(
  input: config + '.meson',
  output: config,
  configuration: config_h
)

output = '\n'
output += '  Build EXPERIMENTAL library libnm-gtk4 for use with GTK4: ' + enable_libnma_gtk4.to_string() + '\n'
output += '  GCR: ' + enable_gcr.to_string() + '\n'
output += '  LTO: ' + get_option('b_lto').to_string() + '\n'
output += '  Linker garbage collection: ' + enable_ld_gc.to_string() + '\n'
message(output)
