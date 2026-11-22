# SPDX-License-Identifier: LGPL-2.1-or-later
# FakePod (ESP32-S31, 460x460 AMOLED, two keys, QMI8658 IMU, AXP2101): the same bsp/*.h API as components/jc3636f118_bsp. Only `make build-fakepod` (-DJC_FAKEPOD=1) builds it;
# the round board's component is empty in that build and this one is empty in every other, so the two never meet at link time.
# What both boards do alike (SD card, PDM mic, the pure helpers, the click decoder) is that component's source, compiled here with this board's bsp_pins.h.
if(NOT (JC_FAKEPOD OR "$ENV{JC_FAKEPOD}"))   # the environment too: the components are first read in a script pass that has no -D values (as JC_LDAC)
    idf_component_register()
else()
    set(JC ../jc3636f118_bsp)   # relative to this component: IDF resolves SRCS and INCLUDE_DIRS from here
    idf_component_register(
        SRCS "src/bsp_i2c.c" "src/bsp_display.c" "src/bsp_co5300.c" "src/bsp_touch.c" "src/bsp_keys.c" "src/bsp_imu.c" "src/bsp_board.c" "src/bsp_dac.c" "src/bsp_pmu.c" "src/bsp_rtc.c"
             "src/bsp_stubs.c" "src/bsp_button.c"
             "${JC}/src/bsp_audio.c" "${JC}/src/bsp_math.c" "${JC}/src/bsp_knob_fsm.c" "${JC}/src/bsp_sdcard.c" "${JC}/src/bsp_mic.c"
        INCLUDE_DIRS "include" "${JC}/include"   # this board's bsp_pins.h / bsp_geom.h first; the headers of the shared API from the round board's folder
        PRIV_INCLUDE_DIRS "src"
        REQUIRES driver esp_timer fatfs sdmmc esp_driver_sdmmc esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_spi esp_lcd lvgl__lvgl espressif__esp_lvgl_adapter espressif__esp_lcd_touch espressif__esp_lcd_touch_cst816s player_core)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC BSP_FAKEPOD_BOARD=1 BSP_DAC_CS43131=1)   # DACFS: bsp_audio.h's default wired ceiling (384 kHz here)
endif()
