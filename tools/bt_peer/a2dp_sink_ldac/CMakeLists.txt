cmake_minimum_required(VERSION 3.22)
# Needs the patched Bluedroid (tools/bt_ldac/make_bt.sh): the patched copy replaces IDF's bt component.
# libfreeaptx (shared with the player) decodes aptX / aptX HD.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../.local/bt_ldac ${CMAKE_CURRENT_LIST_DIR}/../../../components/libfreeaptx)
if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/../../../.local/bt_ldac/bt/CMakeLists.txt)
    message(FATAL_ERROR "run tools/bt_ldac/make_bt.sh first (patched bt component missing)")
endif()
# which vendor codecs the peer offers besides SBC: bit 0 LDAC, bit 1 aptX HD, bit 2 aptX (idf.py -DPEER_CODECS=6)
set(PEER_CODECS 7 CACHE STRING "vendor codecs offered: 1 LDAC, 2 aptX HD, 4 aptX")
# 1: offer 44.1 kHz only (tests the player's 44.1 path); 0: 44.1 and 48
set(PEER_44K_ONLY 0 CACHE STRING "offer 44.1 kHz only")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(COMPILE_DEFINITIONS "PEER_CODECS=${PEER_CODECS}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "PEER_44K_ONLY=${PEER_44K_ONLY}" APPEND)
project(a2dp_sink_ldac)
