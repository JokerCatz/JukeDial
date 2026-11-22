# SPDX-License-Identifier: LGPL-2.1-or-later
set(HB_PC "${CMAKE_CURRENT_LIST_DIR}/../../../../components/player_core")
idf_component_register(SRCS "main.c" "flac_part.c" "dsp_part.c" "${HB_PC}/src/pc_dec2.c" "${HB_PC}/src/pc_rsq.c"
    INCLUDE_DIRS "." "../../../../components/player_core/third_party/dr_flac" "../../../../components/player_core/include"
    REQUIRES esp_driver_i2s esp_driver_pcnt esp_driver_gpio esp_timer esp_hw_support esp_hal_clock esp_hal_i2s esp_partition hal soc heap)
# FLAC fixtures: generated on the host at build time (ffmpeg), never in git; flashed into the "fix" partition with `idf.py flash`.
# Compression level: idf.py -DHB_LEVEL=8 ... (default 5).
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()
if(NOT DEFINED HB_LEVEL)
    set(HB_LEVEL 5)
endif()
set(HB_FIX "${CMAKE_BINARY_DIR}/fixtures_l${HB_LEVEL}.bin")
add_custom_command(OUTPUT "${HB_FIX}"
    COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/../gen_fixtures.py" ${HB_LEVEL} "${HB_FIX}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../gen_fixtures.py" VERBATIM)
add_custom_target(hb_fixtures ALL DEPENDS "${HB_FIX}")
esptool_py_flash_to_partition(flash "fix" "${HB_FIX}")
