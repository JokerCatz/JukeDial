# SPDX-License-Identifier: LGPL-2.1-or-later
file(GLOB APP_SRCS "${CMAKE_CURRENT_LIST_DIR}/*.c" "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
# bt/wifi are listed unconditionally: Kconfig values are not known when requirements are expanded.
# libldac / libfreeaptx: used by the vendor codecs of `make build-ldac` only (unreferenced otherwise)
# board support: the round board's component, or the FakePod's (`make build-fakepod`, -DJC_FAKEPOD=1; the other one is registered empty)
if(JC_FAKEPOD OR "$ENV{JC_FAKEPOD}")   # the environment too: the requirements are expanded in a script pass without -D values
    set(bsp_comp fakepod_bsp)
else()
    set(bsp_comp jc3636f118_bsp)
endif()
set(reqs ${bsp_comp} spi_flash player_core gme esp_driver_jpeg libldac libfreeaptx esp_timer fatfs nvs_flash esp_system esp_lcd freertos bt esp_wifi esp_netif esp_event esp_driver_usb_serial_jtag esp_driver_uart espressif__cjson esp_http_client esp-tls esp_partition espressif__freetype espressif__libpng mbedtls esp_app_format esp_hid esp_pm esp_hw_support)
idf_component_register(SRCS ${APP_SRCS} INCLUDE_DIRS "." "ui" "lvos" REQUIRES ${reqs} LDFRAGMENTS "linker.lf" EMBED_FILES "sounds/alarm.mp3" "sounds/chime.mp3" "assets/slot_syms.bin"
    "assets/lists/lunch.zip" "assets/lists/people.zip" "assets/lists/ideas.zip" "assets/vocab/sample.zip")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation -Wno-format-truncation)
# 外接模式 T3: the IDF HID device component (BLE HOGP, used in the external mode only) keeps its small statics out of internal RAM too (no RISC-V small data: .sbss -> .bss; linker.lf puts what is left of it in PSRAM)
idf_component_get_property(hid_lib esp_hid COMPONENT_LIB)
target_compile_options(${hid_lib} PRIVATE -msmall-data-limit=0)
# `make lint-app`: gcc -fanalyzer over this component only (build dir app/build-lint, -DJC_LINT_APP=1); off in every other build
if(JC_LINT_APP)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fanalyzer)
endif()
# fortune pages: no RISC-V small-data sections, so their small statics follow .bss into PSRAM (linker.lf) too
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/scr_fortune.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_tools.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/reel.c" "${CMAKE_CURRENT_LIST_DIR}/ui/ledrim.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_lists.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/scr_vocab.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_bubbles.c" "${CMAKE_CURRENT_LIST_DIR}/ui/ui_bubbles.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/scr_inst.c" "${CMAKE_CURRENT_LIST_DIR}/ui/inst_pads.c" "${CMAKE_CURRENT_LIST_DIR}/ui/inst_bars.c"
    "${CMAKE_CURRENT_LIST_DIR}/ui/scr_ext.c" "${CMAKE_CURRENT_LIST_DIR}/ext_mode.c" "${CMAKE_CURRENT_LIST_DIR}/ext_hid_send.c" "${CMAKE_CURRENT_LIST_DIR}/ext_ble.c" "${CMAKE_CURRENT_LIST_DIR}/ext_uac.c" "${CMAKE_CURRENT_LIST_DIR}/ext_audio.c" "${CMAKE_CURRENT_LIST_DIR}/ext_usbdesc.c" "${CMAKE_CURRENT_LIST_DIR}/ext_xport.c" "${CMAKE_CURRENT_LIST_DIR}/ui/ui_safe.c" "${CMAKE_CURRENT_LIST_DIR}/rate_test.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_ratetest.c" "${CMAKE_CURRENT_LIST_DIR}/bt_remote.c"
    "${CMAKE_CURRENT_LIST_DIR}/chipbench_ctl.c" "${CMAKE_CURRENT_LIST_DIR}/sd_format.c" "${CMAKE_CURRENT_LIST_DIR}/sd_hw.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_card.c" "${CMAKE_CURRENT_LIST_DIR}/ui/ui_code_pad.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_sfxbank.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_covers.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_langpick.c"
    "${CMAKE_CURRENT_LIST_DIR}/bt_meta.c" "${CMAKE_CURRENT_LIST_DIR}/bt_meta_avrc.c" "${CMAKE_CURRENT_LIST_DIR}/jpeg_route.c" "${CMAKE_CURRENT_LIST_DIR}/thumb_svc.c" "${CMAKE_CURRENT_LIST_DIR}/album_svc.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # jpeg_route.c, thumb_svc.c, album_svc.c: the cover and album paths (hires 8d: their small statics pay for the JPEG engine driver's 60 B of internal RAM); scr_usb.c, ui_safe.c: the USB mode page and the safe screen, no internal RAM in the other modes; rate_test.c, scr_ratetest.c: the rate test's state (hires T11, internal RAM 0); bt_remote.c: the headset remote's state; sd_format.c, sd_hw.c, scr_card.c, ui_code_pad.c: the card format (cardfmt, internal RAM 0); bt_meta*.c: the phone's track in speaker mode (AVRCP 1.6)
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/card_test.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_cardtest.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # 記憶卡診斷: the service and its page keep their statics in PSRAM (internal RAM 0)
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/cast_mode.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_cast.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # the broadcast radio boot and its page keep their statics in PSRAM
# games (G0): the hub, the board piece and every game page (ui/scr_game_*.c, found by name: a new game adds no line here) keep their statics in PSRAM;
# G0b: the shared game pieces (ui/game_*.c: style table, effects, result card, the game ring) too; G1: the field piece ui/ui_arena.c
file(GLOB GAME_SRCS "${CMAKE_CURRENT_LIST_DIR}/ui/scr_game_*.c" "${CMAKE_CURRENT_LIST_DIR}/ui/game_*.c")
set_source_files_properties(${GAME_SRCS} "${CMAKE_CURRENT_LIST_DIR}/ui/scr_games.c" "${CMAKE_CURRENT_LIST_DIR}/ui/ui_board.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/imu_svc.c" "${CMAKE_CURRENT_LIST_DIR}/keys_svc.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # F1b: the FakePod's keys and tilt services keep their statics in PSRAM (compiled out on the round board)
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_frame.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # F1a2: the page frame pool keeps its statics in PSRAM
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_exit.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G4b: the shared exit keeps its statics in PSRAM
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_sectors.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G1: the sectors piece (四色記憶) too
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_arena.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G1: the arena piece (環形打磚塊) too
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_dial.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G2: the dial piece (保險箱開鎖) too
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_cards.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G4: the cards piece (骨牌) too
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_beat.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G3: the beat feed keeps its static in PSRAM (the game page is found by name above)
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/ui_qs.c" "${CMAKE_CURRENT_LIST_DIR}/ui/ui_tour.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # GS: quick settings and the tutorial keep their statics in PSRAM
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/gamesave_svc.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_counter.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G2: games.txt's file glue and the counter page keep their statics in PSRAM
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/drink_svc.c" "${CMAKE_CURRENT_LIST_DIR}/timers_svc.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_timers.c" "${CMAKE_CURRENT_LIST_DIR}/ui/tface.c" "${CMAKE_CURRENT_LIST_DIR}/ui/face_art.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_drink.c" "${CMAKE_CURRENT_LIST_DIR}/ui/scr_habits.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G7: 喝水提醒 and 習慣打卡 keep their statics in PSRAM
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/scr_ambient.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G5: the ambient page keeps its statics in PSRAM
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/scr_breath.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # G7: the breathing coach keeps its statics in PSRAM
# flash fonts (fontpak.c): FreeType opens "/flashfont/..." as a memory stream on the mapped partition
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=FT_Stream_Open")
# serial console (usj_link.c): our "host present?" test replaces IDF's, whose false "unplugged" ticks dropped replies
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=usb_serial_jtag_is_connected")
# crash log (crashlog.c): PC / RA / mcause into RTC memory, then IDF's own panic handler (spike on the peer board 2026-11-12)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
# parking a core (core_park.c, the one place for it): the core parked during a flash operation switches its branch predictor off first (psram-hunt 2026-11-19; checked by tools/check/check_flash_park.py)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_ipc_call_nonblocking")

set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/ui/scr_tuner.c" "${CMAKE_CURRENT_LIST_DIR}/tuner_svc.c" PROPERTIES COMPILE_OPTIONS "-msmall-data-limit=0")   # the tuner pages and its listening service keep their statics in PSRAM (internal RAM 0)
