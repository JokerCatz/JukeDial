# SPDX-License-Identifier: LGPL-2.1-or-later
# Portable player core: no ESP-IDF or LVGL dependencies (host-tested in host_test/).
file(GLOB PC_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
idf_component_register(SRCS ${PC_SRCS} INCLUDE_DIRS "include" "third_party/dr_flac" PRIV_INCLUDE_DIRS "third_party/minimp3" "third_party/miniz" "third_party/tomlc99" "src" PRIV_REQUIRES espressif__cjson libxmp libmpcdec libtta libhvl libcrsid libadplug)
# player_core does not include sdkconfig.h: the switch reaches its sources as a plain define
if(CONFIG_FX_VELVET_ROOM)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_FX_VELVET_ROOM=1)
endif()
