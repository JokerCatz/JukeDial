# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)
# Needs the patched Bluedroid (tools/bt_peer/avrc_peer/make_bt.sh): the patched copy replaces IDF's bt component.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../.local/bt_avrc)
if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/../../../.local/bt_avrc/bt/CMakeLists.txt)
    message(FATAL_ERROR "run tools/bt_peer/avrc_peer/make_bt.sh first (patched bt component missing)")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(avrc_peer)
