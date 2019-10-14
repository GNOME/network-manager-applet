utils_inc = include_directories('.')

incs = [
  top_inc,
  shared_inc
]

deps = [
  gtk_dep,
  libnm_dep
]

libutils_libnm = static_library(
  'utils-libnm',
  sources: shared_sources + files(
    'utils.c',
  ),
  include_directories: incs,
  dependencies: deps
)

libutils_libnm_dep = declare_dependency(
  link_with: libutils_libnm,
  include_directories: utils_inc
)

test_unit = 'test-utils'

exe = executable(
  test_unit,
  'tests/' + test_unit + '.c',
  include_directories: incs,
  dependencies: deps,
  link_with: libutils_libnm
)

test(test_unit, exe)
