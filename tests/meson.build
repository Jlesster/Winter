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
  files('utf8_test.cpp'),
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
