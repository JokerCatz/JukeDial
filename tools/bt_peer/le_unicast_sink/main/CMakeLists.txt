# pc_btcodec.c: the tone analyser shared with the player (host-tested there, no IDF deps)
set(PC ${CMAKE_CURRENT_LIST_DIR}/../../../../components/player_core)
idf_component_register(SRCS "main.c" "peripheral.c" "${PC}/src/pc_btcodec.c"
                       INCLUDE_DIRS "." "${PC}/include"
                       REQUIRES bt nvs_flash esp_timer)
