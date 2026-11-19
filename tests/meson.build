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

