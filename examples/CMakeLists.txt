cmake_minimum_required(VERSION 3.0)

add_executable(imginfo imginfo.c)
target_include_directories( imginfo PRIVATE .. )
target_link_libraries(imginfo impxy)

