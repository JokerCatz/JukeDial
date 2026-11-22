# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.16)
# the codec library is shared with the player: components/libldac; -DLDAC_PROF=1 swaps in prof/libldac (same
# sources, stage timers)
if(LDAC_PROF)
    set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/prof/libldac)
else()
    set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../components/libldac)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ldac_bench)
