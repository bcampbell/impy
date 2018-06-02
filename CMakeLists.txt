cmake_minimum_required(VERSION 3.0)

project(libimpy_project)

find_package(PNG REQUIRED)
find_package(GIF 5 REQUIRED)
find_package(JPEG REQUIRED)


add_library(impy STATIC bmp_read.c bmp_write.c bundle.c convert.c gif.c iff.c im.c img.c img_default.c io.c jpeg.c pcx.c png_read.c png_write.c targa.c util.c)


target_include_directories(impy PUBLIC ${PNG_INCLUDE_DIR} PUBLIC ${GIF_INCLUDE_DIR} PUBLIC ${JPEG_INCLUDE_DIR})
target_link_libraries(impy ${PNG_LIBRARY} ${GIF_LIBRARIES} ${JPEG_LIBRARIES})

install(TARGETS impy DESTINATION lib)
install(FILES impy.h DESTINATION include)


add_subdirectory(examples)
