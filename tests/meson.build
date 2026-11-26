san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']

piece_test = executable(
  'piece_test',
  files('../src/piece.cpp', 'piece_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('piece', piece_test)

utf8_test = executable(
  'utf8_test',
  files('../src/grapheme.cpp', 'utf8_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('utf8', utf8_test)

registry_test = executable(
  'registry_test',
  files('../src/registry.cpp', 'registry_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('registry', registry_test)

chord_test = executable(
  'chord_test',
  files('../src/chord.cpp', '../src/registry.cpp', 'chord_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('chord', chord_test)

fold_test = executable(
  'fold_test',
  files('../src/fold.cpp', '../src/registry.cpp', 'fold_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('fold', fold_test)

vt_test = executable(
  'vt_test',
  files('../src/vt.cpp', 'vt_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('vt', vt_test)

pty_test = executable(
  'pty_test',
  files('../src/loop.cpp', '../src/pty.cpp', 'pty_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('pty', pty_test)

term_keys_test = executable(
  'term_keys_test',
  files('../src/term_keys.cpp', 'term_keys_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('term_keys', term_keys_test)

num_test = executable(
  'num_test',
  files('../src/num.cpp', 'num_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('num', num_test)

# The tests in the subdirectories include headers as "registry.hpp" or
# "../src/foo.hpp", which need src itself on the include path.
src_incls = include_directories('../src')
py = import('python').find_installation('python3')

grapheme_test = executable(
  'grapheme_test',
  files('../src/grapheme.cpp', 'grapheme_test.cpp'),
  cpp_args: san,
  link_args: san,
)
test('grapheme', grapheme_test)

md_test = executable(
  'md_test',
  files(
    '../src/columns.cpp',
    '../src/grapheme.cpp',
    '../src/markdown.cpp',
    '../src/virt.cpp',
    'md_test.cpp',
  ),
  cpp_args: san,
  link_args: san,
)
test('md', md_test)

motion_test = executable(
  'motion_test',
  files(
    '../src/fold.cpp',
    '../src/motion.cpp',
    '../src/piece.cpp',
    'motion_test.cpp',
  ),
  cpp_args: san,
  link_args: san,
)
test('motion', motion_test)

options_test = executable(
  'options_test',
  files(
    '../src/num.cpp',
    '../src/options.cpp',
    'options_test.cpp',
    'test_stubs.cpp',
  ),
  cpp_args: san,
  link_args: san,
)
test('options', options_test)

keymap_test = executable(
  'keymap_test',
  files('../src/registry.cpp', 'keymap/keymap_test.cpp'),
  include_directories: src_incls,
  cpp_args: san,
  link_args: san,
)
test('keymap', keymap_test)

marks_test = executable(
  'marks_test',
  files('../src/marks.cpp', '../src/piece.cpp', 'marks/marks_test.cpp'),
  include_directories: src_incls,
  cpp_args: san,
  link_args: san,
)
test('marks', marks_test)

reg_test = executable(
  'reg_test',
  files('../src/reg.cpp', 'reg/reg_test.cpp', 'test_stubs.cpp'),
  include_directories: src_incls,
  cpp_args: san,
  link_args: san,
)
test('reg', reg_test)

if 'c' in grammars
  tsq_test = executable(
    'tsq_test',
    files('../src/tsq.cpp', 'ts/tsq_test.cpp'),
    include_directories: src_incls,
    dependencies: [ts_dep, dependency('tree-sitter-c')],
    cpp_args: san,
    link_args: san,
  )
  test('tsq', tsq_test)
endif

# Lua API tests: the Lua half of the runtime is extracted from
# script_runtime.cpp so the same source that ships with the editor is tested.
lua = find_program('lua5.4', required: false)
if not lua.found()
  lua = find_program('lua', required: false)
endif

runtime_lua = custom_target(
  'runtime_lua',
  input: '../src/script_runtime.cpp',
  output: 'runtime.lua',
  command: [
    py,
    '-c', 'import pathlib,sys; s=pathlib.Path(sys.argv[1]).read_text(); '
    + 'a=s.index(\'R"LUA(\')+6; b=s.index(\')LUA";\',a); '
    + 'pathlib.Path(sys.argv[2]).write_text(s[a:b])',
    '@INPUT@',
    '@OUTPUT@',
  ],
)

if lua.found()
  lua_env = ['WT=' + meson.current_build_dir()]

  test('lua-runtime', lua, args: [files('lua/runtime_test.lua'), runtime_lua])

  lua_harnesses = [
    [
      'fs',
      ['lua/fs_harness.cpp'],
      ['../src/script_fs.cpp'],
      'lua/fs_test.lua',
    ],
    [
      'json',
      ['lua/json_harness.cpp'],
      ['../src/script_json.cpp', '../src/lua_json.cpp', '../src/json.cpp'],
      'lua/json_test.lua',
    ],
    [
      'reg',
      ['lua/reg_harness.cpp'],
      ['../src/script_reg.cpp', '../src/reg.cpp', 'test_stubs.cpp'],
      'lua/reg_test.lua',
    ],
    [
      'keys',
      ['lua/keys_harness.cpp'],
      ['../src/script_keys.cpp', '../src/registry.cpp'],
      'lua/keys_test.lua',
    ],
  ]
  foreach h : lua_harnesses
    exe = executable(
      'lua_' + h[0],
      files(h[1]) + files(h[2]),
      include_directories: src_incls,
      dependencies: lua_dep,
      cpp_args: san,
      link_args: san,
    )
    test(
      'lua-' + h[0],
      exe,
      args: [files(h[3])],
      env: lua_env,
    )
  endforeach

  if 'c' in grammars
    lua_ts = executable(
      'lua_ts',
      files('lua/ts_harness.cpp')
      + files('../src/script_ts.cpp', '../src/tsq.cpp'),
      include_directories: src_incls,
      dependencies: [lua_dep, ts_dep, dependency('tree-sitter-c')],
      cpp_args: san,
      link_args: san,
    )
    test(
      'lua-ts',
      lua_ts,
      args: [files('lua/ts_test.lua'), lua_ts],
      env: lua_env,
    )
  endif
endif

# The differential test looks for vt_test next to its own file, so run a copy
# from the build tree, where the binary lives.
tmux = find_program('tmux', required: false)
if tmux.found()
  vt_diff = configure_file(
    input: 'vt_diff_test.py',
    output: 'vt_diff_test.py',
    copy: true,
  )
  test(
    'vt-diff',
    py,
    args: [vt_diff, '0', '5', '1'],
    env: ['TMUX_TMPDIR=' + meson.current_build_dir()],
    timeout: 120,
    depends: vt_test,
  )
endif

