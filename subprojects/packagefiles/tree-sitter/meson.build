project('tree-sitter', 'c', default_options: ['c_std=c11'])

lib = static_library(
  'tree-sitter',
  'lib/src/lib.c',
  include_directories: include_directories('lib/include', 'lib/src'),
  c_args: ['-D_POSIX_C_SOURCE=200112L', '-D_DEFAULT_SOURCE'],
)

dep = declare_dependency(link_with: lib, include_directories: include_directories('lib/include'))
meson.override_dependency('tree-sitter', dep)
