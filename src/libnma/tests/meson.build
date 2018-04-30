tests = [
  'bar-code',
  'cert-chooser',
  'mobile-wizard',
  'run-vpn',
  'vpn-password-dialog',
  'wifi-dialog',
  'wifi-dialog-secrets',
]

foreach tst : tests
  executable(
    tst,
    tst + '.c',
    include_directories: incs,
    dependencies: [ gtk_dep, libnm_dep, libnma_dep ],
    c_args: cflags,
    install: false
  )

  if enable_libnma_gtk4
    executable(
      tst + '-gtk4',
      tst + '.c',
      include_directories: incs,
      dependencies: [ gtk4_dep, libnm_dep, libnma_gtk4_dep ],
      c_args: cflags,
      install: false
    )
  endif
endforeach
