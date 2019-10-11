wireless_security_inc = include_directories('.')

sources = [version_header] + files(
  'eap-method.c',
  'eap-method-fast.c',
  'eap-method-leap.c',
  'eap-method-peap.c',
  'eap-method-simple.c',
  'eap-method-tls.c',
  'eap-method-ttls.c',
  'helpers.c',
  'wireless-security.c',
  'ws-dynamic-wep.c',
  'ws-leap.c',
  'ws-sae.c',
  'ws-wep-key.c',
  'ws-wpa-eap.c',
  'ws-wpa-psk.c'
)

resource_data = files(
  'eap-method-fast.ui',
  'eap-method-leap.ui',
  'eap-method-peap.ui',
  'eap-method-simple.ui',
  'eap-method-tls.ui',
  'eap-method-ttls.ui',
  'ws-dynamic-wep.ui',
  'ws-leap.ui',
  'ws-sae.ui',
  'ws-wep-key.ui',
  'ws-wpa-eap.ui',
  'ws-wpa-psk.ui'
)

sources += gnome.compile_resources(
  'ws-resources',
  'ws.gresource.xml',
  dependencies: resource_data
)

incs = [
  top_inc,
  shared_inc,
  src_inc
]

deps = [
  gtk_dep,
  libnm_dep,
  libutils_libnm_dep
]

libwireless_security_libnm = static_library(
  'wireless-security-libnm',
  sources: sources,
  include_directories: incs,
  dependencies: deps
)

if enable_libnma_gtk4
  deps = [
    gtk4_dep,
    libnm_dep,
    libutils_libnm_dep
  ]

  libwireless_security_libnma_gtk4 = static_library(
    'wireless-security-libnma-gtk4',
    sources: sources,
    include_directories: incs,
    dependencies: deps,
    c_args: cflags
  )
endif
