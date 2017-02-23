cmake_minimum_required(VERSION 3.0)

project(libimpxy_project)

find_package(PNG)
find_package(GIF 5)

add_library(impxy STATIC bmp.c  bundle.c  convert.c  gif.c  iff.c  im.c  img.c  img_default.c  io.c  pcx.c png_read.c  png_write.c  util.c)

install(TARGETS impxy DESTINATION lib)
install(FILES impxy.h DESTINATION include)

#add_executable(imginfo examples/imginfo.c)
#target_link_libraries(imginfo impxy )

