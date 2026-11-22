# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)
# Bluetooth (speaker / headphones / Auracast) is part of the player: its overlay is in the default build
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.bt")
endif()
# LDAC (stage 1, link test): opt-in build with the patched Bluedroid from tools/bt_ldac (`make build-ldac`).
# -DJC_LDAC=1 or JC_LDAC=1 in the environment; the default build is unchanged.
if(JC_LDAC OR "$ENV{JC_LDAC}")
    set(JC_LDAC_BT ${CMAKE_CURRENT_LIST_DIR}/../.local/bt_ldac)
    if(NOT EXISTS ${JC_LDAC_BT}/bt/CMakeLists.txt)
        message(FATAL_ERROR "JC_LDAC: run tools/bt_ldac/make_bt.sh first (patched bt component missing)")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS ${JC_LDAC_BT})   # a project component named bt replaces IDF's
endif()
# exFAT: FatFs from tools/fatfs_exfat (a generated, patched copy of IDF's fatfs). The script runs here at every configure (a no-op when its
# stamp matches), so a direct `idf.py build`, a fresh clone and an IDF upgrade all regenerate; it fails the configure when an edit no longer
# matches. A project component with an IDF component's name replaces IDF's.
set(JC_FATFS_DIR ${CMAKE_CURRENT_LIST_DIR}/../.local/fatfs_exfat)
execute_process(COMMAND env IDF_PATH=$ENV{IDF_PATH} bash ${CMAKE_CURRENT_LIST_DIR}/../tools/fatfs_exfat/make_fatfs.sh
                RESULT_VARIABLE JC_FATFS_RC OUTPUT_VARIABLE JC_FATFS_OUT ERROR_VARIABLE JC_FATFS_ERR)
if(NOT JC_FATFS_RC EQUAL 0 OR NOT EXISTS ${JC_FATFS_DIR}/fatfs/CMakeLists.txt)
    message(FATAL_ERROR "tools/fatfs_exfat/make_fatfs.sh failed (${JC_FATFS_RC}):\n${JC_FATFS_OUT}${JC_FATFS_ERR}")
endif()
list(APPEND EXTRA_COMPONENT_DIRS ${JC_FATFS_DIR})
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(JC_LDAC OR "$ENV{JC_LDAC}")
    idf_build_set_property(COMPILE_DEFINITIONS "JC_LDAC=1" APPEND)
endif()
# no build host path inside the image: the web flasher publishes app.bin (tools/web_publish.py refuses an image with /home/<user>/ in it). IDF's reproducible-build option maps
# the project and IDF folders; our own components live beside the project, so the whole repository is mapped too.
get_filename_component(JC_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
idf_build_set_property(COMPILE_OPTIONS "-ffile-prefix-map=${JC_REPO_ROOT}=/repo" APPEND)
# Version: ../version.txt (YYYY.MM.DD, one line) is the release version; the image's PROJECT_VER (app_desc, crash line fw=, web manifest) is
# "<version>-<short git hash>", so a development image stays identifiable (no git: just the version). the base design notes: how to bump it.
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../version.txt JC_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../version.txt)
execute_process(COMMAND git -C ${CMAKE_CURRENT_LIST_DIR} rev-parse --short=7 HEAD OUTPUT_VARIABLE JC_BUILD_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C ${CMAKE_CURRENT_LIST_DIR} rev-parse --git-path HEAD OUTPUT_VARIABLE JC_GIT_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(JC_BUILD_HASH)
    set(PROJECT_VER "${JC_VERSION}-${JC_BUILD_HASH}")
    get_filename_component(JC_GIT_HEAD "${JC_GIT_HEAD}" ABSOLUTE BASE_DIR ${CMAKE_CURRENT_LIST_DIR})   # relative to the app folder, or absolute (a worktree)
    if(EXISTS ${JC_GIT_HEAD})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${JC_GIT_HEAD})
    endif()
else()
    set(PROJECT_VER "${JC_VERSION}")
endif()
project(jc_player)
# LVGL includes CONFIG_LV_OS_CUSTOM_INCLUDE from its own sources: give it our OS layer's directory
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_include_directories(${lvgl_lib} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/main/lvos)
target_sources(${lvgl_lib} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/main/lvos/lv_os_pinned.c)   # inside LVGL's archive: link order
# Bluedroid trips -Wstringop-truncation at -O2 on this toolchain: relax it for the bt component only,
# instead of building the whole player at -Os (the memtest overlay did that)
if(CONFIG_BT_ENABLED)
    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    target_compile_options(${bt_lib} PRIVATE -Wno-error=stringop-truncation -Wno-stringop-truncation)
endif()
# `make flash-fonts`: the partition table + the font pack (build/fonts.bin from tools/gen/make_fontpak.py) at the
# "fonts" partition's offset, in one esptool run; app-flash never writes either (spec 2026-11-11-flash-font-design.md)
idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(fonts-flash "${main_args}" "${sub_args}")
esptool_py_flash_target_image(fonts-flash partition-table "${CONFIG_PARTITION_TABLE_OFFSET}" "${CMAKE_BINARY_DIR}/partition_table/partition-table.bin")
esptool_py_flash_to_partition(fonts-flash fonts "${CMAKE_BINARY_DIR}/fonts.bin")
add_dependencies(fonts-flash partition-table)
