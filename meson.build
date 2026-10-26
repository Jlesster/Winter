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
lua_dep = dependency('lua5.4', method: 'pkg-config')
if not lua_dep.found()
  lua_dep = dependency('lua', version: '>=5.4', method: 'pkg-config')
endif

fs = import('fs')
grammars = get_option('ts_grammars')
lang_deps = []
lang_syms = []
queries = ''
indents = ''
foreach g : grammars
  sp = subproject('tree-sitter-' + g)
  n = g.replace('-', '_')
  queries += '{"@0@", R"scm(@1@)scm"},\n'.format(n, sp.get_variable('highlights_scm'))
  ifile = 'queries' / n / 'indents.scm'
  if fs.exists(ifile)
    indents += '{"@0@", R"scm(@1@)scm"},\n'.format(n, fs.read(ifile))
  endif
  lang_syms += 'X(@0@)'.format(n)
  lang_deps += dependency('tree-sitter-' + g)
endforeach
configure_file(
  input: 'src/ts_queries.inc.in',
  output: 'ts_queries.inc',
  configuration: configuration_data({'QUERIES': queries}),
)
configure_file(
  input: 'src/ts_indents.inc.in',
  output: 'ts_indents.inc',
  configuration: configuration_data({'INDENTS': indents}),
)

add_project_arguments('-DWINTER_TS_LANGS(X)=' + ' '.join(lang_syms), language: 'cpp')

deps = [
  dependency('trux'),
  ts_dep,
] + lang_deps + [lua_dep]

sources = files(
  'src/buffer.cpp',
  'src/buflist.cpp',
  'src/builtin_actions.cpp',
  'src/builtin_motions.cpp',
  'src/columns.cpp',
  'src/command.cpp',
  'src/diag.cpp',
  'src/edit.cpp',
  'src/editor.cpp',
  'src/events.cpp',
  'src/file.cpp',
  'src/gutter.cpp',
  'src/indent.cpp',
  'src/input.cpp',
  'src/job.cpp',
  'src/json.cpp',
  'src/loop.cpp',
  'src/lsp_client.cpp',
  'src/lsp_config.cpp',
  'src/lsp_edit.cpp',
  'src/lsp_reader.cpp',
  'src/lsp_sync.cpp',
  'src/lua_json.cpp',
  'src/main.cpp',
  'src/motion.cpp',
  'src/normal.cpp',
  'src/operator.cpp',
  'src/options.cpp',
  'src/parser.cpp',
  'src/piece.cpp',
  'src/prompt.cpp',
  'src/registry.cpp',
  'src/render.cpp',
  'src/script.cpp',
  'src/search.cpp',
  'src/shell.cpp',
  'src/state.cpp',
  'src/syntax.cpp',
  'src/syntax_indent.cpp',
  'src/terminal.cpp',
  'src/undofile.cpp',
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
