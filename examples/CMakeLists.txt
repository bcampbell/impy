cmake_minimum_required(VERSION 3.0)


add_executable(imginfo imginfo.c)
target_include_directories( imginfo PRIVATE .. )
target_link_libraries(imginfo impy)

include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)
add_executable(impyview impyview.c)
target_include_directories( impyview PRIVATE .. PRIVATE ${SDL2_INCLUDE_DIRS} )
target_link_libraries(impyview impy ${SDL2_LIBRARIES})

