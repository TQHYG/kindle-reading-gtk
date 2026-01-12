project('kindle-reading-gtk', 'cpp', version: 'v1.0.0', default_options: ['cpp_std=c++17'], meson_version: '>=1.1')

# Define dependencies we want
gtk_dep = dependency('gtk+-2.0')

###
# Project definition
###
sources = files(
    './src/main.cpp',
    './src/daily.cpp',
    './src/dataprocess.cpp',
    './src/month.cpp',
    './src/overview.cpp',
    './src/settingsui.cpp',
    './src/share.cpp',
    './src/utils.cpp',
    './src/week.cpp',
    './thirdparty/qrcodegen/cpp/qrcodegen.cpp'
)

include_dirs = include_directories(
  './src/include/',
  './thirdparty/qrcodegen/cpp/'
)

executable('kindle-reading-gtk', sources, include_directories: include_dirs, dependencies: [gtk_dep], cpp_args: '-static-libstdc++', link_args: '-static-libstdc++')
