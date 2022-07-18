project('impy', 'c')

srcs = [
  'bmp_read.c',
  'bmp_write.c',
  'convert.c',
  'generic_read.c',
  'gif_read.c',
  'gif_write.c',
  'iff.c',
  'im.c',
  'im_read.c',
  'im_write.c',
  'img.c',
  'io.c',
  'jpeg.c',
  'pcx.c',
  'png_read.c',
  'png_write.c',
  'targa.c',
  'util.c',
]
cxx = meson.get_compiler('c')
png_dep = dependency('libpng')
gif_dep = [ cxx.find_library('gif') ]
jpeg_dep = [ cxx.find_library('jpeg') ]

impylib = library(
  meson.project_name(),
  srcs,
  dependencies: [png_dep, gif_dep, jpeg_dep,],
  install: true,
)

subdir('examples')

# Make this library usable as a Meson subproject.
project_dep = declare_dependency(
  link_with : impylib
)
set_variable(meson.project_name() + '_dep', project_dep)

pkg_mod = import('pkgconfig')
pkg_mod.generate(
  name : meson.project_name(),
  filebase : meson.project_name(),
  description : 'Multi-format image loading and saving',
  subdirs : meson.project_name(),
  libraries : impylib,
)

install_headers('impy.h') 

