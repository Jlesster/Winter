project(
  'winter',
  ['c', 'cpp'],
  version: '0.0.1',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=debugoptimized',
  ],
)

sub_opts = ['warning_level=0', 'werror=false']
ts_dep = dependency('tree-sitter', default_options: sub_opts)

grammars = get_option('ts_grammars')
lang_deps = []
lang_syms = []
queries = ''
foreach g : grammars
  sp = subproject('tree-sitter-' + g)
  queries += '{"@0@", R"scm(@1@)scm"},\n'.format(g.replace('-', '_'), sp.get_variable('highlights_scm'))
  lang_syms += 'X(@0@)'.format(g.replace('-', '_'))
  lang_deps += dependency('tree-sitter-' + g)
endforeach
configure_file(
  input: 'src/ts_queries.inc.in',
  output: 'ts_queries.inc',
  configuration: configuration_data({'QUERIES': queries}),
)

add_project_arguments('-DWINTER_TS_LANGS(X)=' + ' '.join(lang_syms), language: 'cpp')

deps = [
  dependency('lua5.4'),
  dependency('trux'),
  ts_dep,
] + lang_deps

sources = files(
  'src/buffer.cpp',
  'src/columns.cpp',
  'src/command.cpp',
  'src/edit.cpp',
  'src/editor.cpp',
  'src/file.cpp',
  'src/input.cpp',
  'src/lua.cpp',
  'src/main.cpp',
  'src/motion.cpp',
  'src/normal.cpp',
  'src/piece.cpp',
  'src/prompt.cpp',
  'src/render.cpp',
  'src/search.cpp',
  'src/syntax.cpp',
  'src/terminal.cpp',
)

incls = include_directories('.')

executable(
  'winter',
  sources: sources,
  include_directories: incls,
  dependencies: deps,
  install: false,
)

subdir('tests')
