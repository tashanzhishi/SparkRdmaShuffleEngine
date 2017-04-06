cmake_minimum_required(VERSION 3.5)
project(SparkRdmaShuffleEngine)
add_subdirectory(src)

include(FindPkgConfig)
pkg_check_modules(GLIB glib-2.0 REQUIRED)
include_directories(${GLIB_INCLUDE_DIRS})