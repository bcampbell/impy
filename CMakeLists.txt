cmake_minimum_required(VERSION 3.0)

project(libimpy_project)

find_package(PNG REQUIRED)
find_package(GIF 5 REQUIRED)


add_library(impy STATIC bmp.c  bundle.c  convert.c  gif.c  iff.c  im.c  img.c  img_default.c  io.c  pcx.c png_read.c  png_write.c  util.c)

target_include_directories(impy PUBLIC ${PNG_INCLUDE_DIR} PUBLIC ${GIF_INCLUDE_DIR})
target_link_libraries(impy ${PNG_LIBRARY} ${GIF_LIBRARIES})

install(TARGETS impy DESTINATION lib)
install(FILES impy.h DESTINATION include)


add_subdirectory(examples)
