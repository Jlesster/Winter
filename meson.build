project(
  'winter',
  'cpp',
  version: '0.0.1',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
  ],
)

sources = files(
  'src/main.cpp',
)

incls = include_directories('.')

deps = [dependency('trux')]

executable(
  'winter',
  sources: sources,
  include_directories: incls,
  dependencies: deps,
  install: false,
)
