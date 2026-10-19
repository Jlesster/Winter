project('ts-grammar', 'c', default_options: ['c_std=c11', 'warning_level=0'])
fs = import('fs')

name = fs.name(meson.current_source_dir())
hl = 'queries/highlights.scm'
highlights_scm = fs.exists(hl) ? fs.read(hl) : ''

srcs = ['src/parser.c']
foreach s : ['src/scanner.c']
  if fs.exists(s)
    srcs += s
  endif
endforeach

lib = static_library(name, srcs, include_directories: include_directories('src'))

meson.override_dependency(name, declare_dependency(link_with: lib))
