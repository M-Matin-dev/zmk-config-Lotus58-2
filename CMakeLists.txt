if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM AND NOT CONFIG_NICE_VIEW_WIDGET_STATUS)
    # CMAKE_SOURCE_DIR is the ZMK app directory; its include/ holds zmk/*.h headers.
    # The shield's CMakeLists.txt normally adds this, but is skipped when
    # NICE_VIEW_WIDGET_STATUS=n, so we must add it ourselves.
    zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
    zephyr_library_include_directories(nice_view)
    zephyr_library_sources(nice_view/custom_status_screen.c)
    zephyr_library_sources(nice_view/widgets/bolt.c)
    zephyr_library_sources(nice_view/widgets/util.c)
    zephyr_library_sources(nice_view/widgets/status.c)
endif()

# Per-connection Mac/Win mode (&os_mode behavior). The keymap runs only on the
# central (left) half, so the peripheral build skips it.
zephyr_include_directories(include)
if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/os_mode.c)
endif()
