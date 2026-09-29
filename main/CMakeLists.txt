# Codeloom 审批器固件。基线 demo 的 demo_*.c / ui_pixel*.c 保留在仓库中作为参考与
# 主机测试对象，但不编进本固件：设备启动直接进入 Codeloom 界面，测试页面不可达。
idf_component_register(
    SRCS "main.c"
         "codeloom_app.c"
         "codeloom_audio.c"
         "codeloom_chime.c"
         "codeloom_diag.c"
         "codeloom_events.c"
         "codeloom_fonts.c"
         "codeloom_http.c"
         "codeloom_protocol.c"
         "codeloom_settings.c"
         "codeloom_setup.c"
         "codeloom_setup_form.c"
         "codeloom_state.c"
         "codeloom_store.c"
         "codeloom_sync.c"
         "codeloom_text.c"
         "codeloom_timing.c"
         "codeloom_ui.c"
         "codeloom_url.c"
         "codeloom_wifi.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_app_format esp_event esp_http_client esp_http_server esp_netif
             esp_timer esp_wifi json mbedtls nvs_flash
)

# Noto Sans SC 子集字体（由 tools/gen_codeloom_fonts.py 生成，来源与许可见 assets/README.md）。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/codeloom_font_14.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/codeloom_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/codeloom_font_22.c"
)
