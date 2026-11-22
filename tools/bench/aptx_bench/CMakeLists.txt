# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.16)
# the codec library is shared with the player: components/libfreeaptx
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../components/libfreeaptx)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aptx_bench)
