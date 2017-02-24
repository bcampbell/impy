cmake_minimum_required(VERSION 3.0)

project(libimpxy_project)

find_package(PNG REQUIRED)
find_package(GIF 5 REQUIRED)


add_library(impxy STATIC bmp.c  bundle.c  convert.c  gif.c  iff.c  im.c  img.c  img_default.c  io.c  pcx.c png_read.c  png_write.c  util.c)

target_include_directories(impxy PUBLIC ${PNG_INCLUDE_DIR} PUBLIC ${GIF_INCLUDE_DIR})
target_link_libraries(impxy ${PNG_LIBRARY} ${GIF_LIBRARIES})

install(TARGETS impxy DESTINATION lib)
install(FILES impxy.h DESTINATION include)

#add_executable(imginfo examples/imginfo.c)
#target_link_libraries(imginfo impxy )


add_subdirectory(examples)
