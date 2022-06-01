inc = include_directories('..')

executable('imginfo', 'imginfo.c',
  include_directories : inc,
  link_with : impylib,)

executable('impyconvert', 'impyconvert.c',
  include_directories : inc,
  link_with : impylib,)

sdl2_dep = dependency('sdl2', required : false)
if sdl2_dep.found()
  executable('impyview', 'impyview.c',
    include_directories : inc,
    link_with : impylib,
    dependencies : sdl2_dep)
endif
