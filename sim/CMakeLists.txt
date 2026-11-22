# SPDX-License-Identifier: LGPL-2.1-or-later
# Desktop simulator. Two targets: lvgl_sim (LVGL built from the firmware's config; rebuilt only when the config changes)
# and sim (our code). Driven by `make sim` / tools/simtools/sim_lvconf.py; the generated config is passed as SIM_LVCONF.
cmake_minimum_required(VERSION 3.16)
project(sim C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # build/compile_commands.json: tools/refactor_snapshot.py reads the sources this binary compiles

if(NOT SIM_LVCONF)
  message(FATAL_ERROR "use `make sim` (it generates the LVGL config and passes -DSIM_LVCONF=...)")
endif()
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../app/managed_components/lvgl__lvgl)
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
  message(FATAL_ERROR "LVGL sources not found at ${LVGL_DIR}: run `make build` once first")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(FT2 REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_BUILD_TYPE Debug)
# Same flags for every object: ASan/UBSan stay on for LVGL too (it is where UI leaks show up).
set(SIM_FLAGS -g -O1 -fno-omit-frame-pointer -funsigned-char -fsanitize=address,undefined -U_FORTIFY_SOURCE -DSIM=1
    -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=implicit-function-declaration)
set(SIM_SAN_LINK -fsanitize=address,undefined)
if(SIM_PERF)   # `make sim-perf`: -O2, no sanitizers, a separate build dir: the render-time measure (tools/simtools/sim_perf.sh) is relative, host only
  set(SIM_FLAGS -O2 -funsigned-char -DSIM=1 -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=implicit-function-declaration)
  set(SIM_SAN_LINK "")
endif()

file(GLOB_RECURSE LVGL_SRC ${LVGL_DIR}/src/*.c)
add_library(lvgl_sim STATIC ${LVGL_SRC})
target_include_directories(lvgl_sim PUBLIC ${LVGL_DIR} ${LVGL_DIR}/src)
target_compile_definitions(lvgl_sim PUBLIC LV_CONF_SKIP "LV_CONF_KCONFIG_EXTERNAL_INCLUDE=\"${SIM_LVCONF}\"")
target_compile_options(lvgl_sim PUBLIC ${SIM_FLAGS})
target_link_libraries(lvgl_sim PUBLIC PkgConfig::SDL2 PkgConfig::FT2 m)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../app/main)
set(PC  ${CMAKE_CURRENT_SOURCE_DIR}/../components/player_core)
set(BSP ${CMAKE_CURRENT_SOURCE_DIR}/../components/jc3636f118_bsp)

# Firmware sources compiled as they are (spec 3.1). The UI is every ui/*.c; inst_ui.c (the instrument app's screens and the UI half of its console) is not in ui/.
file(GLOB UI_SRC ${APP}/ui/*.c)
set(FW_SRC ${UI_SRC}
    ${APP}/speed_svc.c ${APP}/out_status.c ${APP}/splash.c ${APP}/boot_front.c
    ${APP}/psram_alloc.c ${APP}/fx_state.c ${APP}/cfg.c ${APP}/vol_out.c ${APP}/lab.c ${APP}/lowmem.c ${APP}/memlog.c ${APP}/stats_svc.c ${APP}/gamesave_svc.c ${APP}/sleep_timer.c
    ${APP}/fortune_text.c ${APP}/lists_ctl.c ${APP}/fonts_ctl.c ${APP}/mp3_ctl.c ${APP}/crashnet.c ${APP}/player_ctl.c ${APP}/chip_ctl.c
    ${APP}/rate_test.c ${APP}/pack_stamp.c ${APP}/sd_format.c ${APP}/color_noise.c ${APP}/library_svc.c ${APP}/ctl.c ${APP}/clock_svc.c ${APP}/timers_svc.c ${APP}/drink_svc.c ${APP}/console.c ${APP}/inst_ui.c ${APP}/bt_meta.c ${APP}/sfx.c ${APP}/tuner_svc.c ${APP}/ext_mode.c ${APP}/ext_hid_send.c ${APP}/ext_xport.c ${APP}/ext_usbdesc.c ${APP}/ext_audio.c)
list(APPEND FW_SRC ${APP}/card_test.c)   # 記憶卡診斷: the service (its lower half is fake_sd.c)
file(GLOB PC_SRC ${PC}/src/*.c)
set(CJSON ${CMAKE_CURRENT_SOURCE_DIR}/../app/managed_components/espressif__cjson/cJSON)
set(XMP   ${CMAKE_CURRENT_SOURCE_DIR}/../components/libxmp)
file(GLOB FAKES ${CMAKE_CURRENT_SOURCE_DIR}/fake_*.c)
set(SIM_SRC main.c sim_clock.c sim_fs.c sim_shot.c sim_cmd.c selftest.c shim_esp_timer.c shim_esp.c shim_bsp.c shim_font.c sim_state.c sim_cover.c sim_boot.c sim_na.c sim_input.c shim_restart.c ${FAKES} embed.S)

# player_core as a static library: only the objects the UI and services reference get linked.
list(FILTER PC_SRC EXCLUDE REGEX "pc_backend_(xmp|mpc|tta|hvl|sid|opl)\\.c$")   # libxmp, libmpcdec and libtta are not linked: fake_backend.c stands in for them
add_library(pc_sim STATIC ${PC_SRC} ${CJSON}/cJSON.c)
add_executable(sim ${SIM_SRC} ${FW_SRC})
if(SIM_FP)   # `make sim-fp`: the square 460x460 panel without a knob (sim/fp/bsp/bsp_geom.h shadows the round board's), a build dir of its own
  target_compile_definitions(sim PRIVATE SIM_FP=1)
  target_include_directories(sim BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/fp)
endif()
target_include_directories(sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${APP} ${APP}/ui ${PC}/include ${PC}/third_party/dr_flac ${PC}/third_party/minimp3 ${PC}/third_party/miniz ${PC}/third_party/tomlc99 ${PC}/src ${BSP}/include ${CJSON})
target_include_directories(pc_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim ${PC}/include ${PC}/third_party/dr_flac ${PC}/third_party/minimp3 ${PC}/third_party/miniz ${PC}/third_party/tomlc99 ${PC}/src ${CJSON} ${XMP}/libxmp/include ${XMP}/port)
target_compile_options(pc_sim PRIVATE ${SIM_FLAGS})
# CONFIG_FX_VELVET_ROOM (components/player_core/Kconfig, default off): the simulator takes it from the tracked sdkconfig.defaults only,
# so a fresh worktree builds the same thing whatever the untracked app/sdkconfig holds. The firmware build gets it from Kconfig.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${APP}/../sdkconfig.defaults)   # a change of that file re-runs cmake
file(STRINGS ${APP}/../sdkconfig.defaults FX_VROOM_LINE REGEX "^CONFIG_FX_VELVET_ROOM=y$")
if(FX_VROOM_LINE)
  target_compile_definitions(pc_sim PRIVATE CONFIG_FX_VELVET_ROOM=1)
  target_compile_definitions(sim PRIVATE CONFIG_FX_VELVET_ROOM=1)
endif()
# The firmware sources are not ours to quiet at the source: int64_t %lld and snprintf truncation notes are host-only noise.
set_source_files_properties(${FW_SRC} PROPERTIES COMPILE_OPTIONS "-Wno-format;-Wno-format-truncation")
enable_language(ASM)
set_source_files_properties(embed.S PROPERTIES COMPILE_OPTIONS "-Wa,-I${APP}")   # .incbin paths in embed.S are relative to app/main
target_link_libraries(sim PRIVATE pc_sim lvgl_sim PkgConfig::PNG)
target_link_options(sim PRIVATE ${SIM_SAN_LINK})
# Link-time wrapping: firmware code keeps calling fopen/time/...; /sdcard paths and the clock are redirected here.
set(SIM_WRAPS time gettimeofday settimeofday fopen open stat opendir remove rename mkdir unlink rmdir access truncate)
foreach(w ${SIM_WRAPS})
  target_link_options(sim PRIVATE -Wl,--wrap=${w})
endforeach()
