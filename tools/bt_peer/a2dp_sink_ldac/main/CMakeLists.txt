# pc_btcodec.c: the LDAC packet check and the tone analyser, shared with the player (host-tested there)
set(PC ${CMAKE_CURRENT_LIST_DIR}/../../../../components/player_core)
idf_component_register(SRCS "main.c" "${PC}/src/pc_btcodec.c"
                    PRIV_REQUIRES bt nvs_flash esp_timer libfreeaptx bt_app_core_utils bredr_app_common_utils
                    INCLUDE_DIRS "." "${PC}/include")
