cmake_minimum_required(VERSION 3.22)
# LE Audio unicast test sink "JC Peer LEA" (based on IDF bap/unicast_server): PACS/ASCS sink, LC3 decode on core 1,
# test-tone check of the left channel (pc_tone_analyse from player_core).
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(le_unicast_sink)
