wireless_security_inc = include_directories('.')

sources = files(
  'eap-method.c',
)

incs = [
  top_inc,
  shared_inc,
  src_inc
]

deps = [
  gtk_dep,
  libnm_dep,
]

libwireless_security_libnm = static_library(
  'wireless-security-libnm',
  sources: sources,
  include_directories: incs,
  dependencies: deps
)
