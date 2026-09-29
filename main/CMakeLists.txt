# 口袋垂钓固件。基线 demo 的 demo_*.c / ui_pixel*.c 保留在仓库中作为参考与主机测试对象，
# 但不编进本固件：设备启动直接进入钓鱼界面，测试页面不可达。
idf_component_register(
    SRCS "main.c"
         "fish_app.c"
         "fish_audio.c"
         "fish_catalog.c"
         "fish_fonts.c"
         "fish_game.c"
         "fish_save.c"
         "fish_sound.c"
         "fish_store.c"
         "fish_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# Noto Sans SC 子集字体（由 tools/gen_fish_fonts.py 生成，来源与许可见 assets/README.md）。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/fish_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/fish_font_24.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/fish_font_40.c"
)

# 8-bit 像素素材：构建时由 tools/gen_fish_sprites.py 生成（约 0.5 MB 的 C 数组不入库），
# 画法、调色板与来源见 assets/README.md。
idf_build_get_property(python PYTHON)
set(fish_sprites_c "${CMAKE_CURRENT_BINARY_DIR}/fish_sprites.c")
add_custom_command(
    OUTPUT "${fish_sprites_c}"
    COMMAND "${python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_fish_sprites.py" --c-out "${fish_sprites_c}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_fish_sprites.py"
            "${CMAKE_CURRENT_LIST_DIR}/fish_catalog.c"
    COMMENT "Generating Pocket Fishing pixel art"
    VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE "${fish_sprites_c}")
