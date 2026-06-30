# gui/ — Qt 6 GUI application for openEB.

set(GUI_SOURCES
    main.cpp
    main_window.cpp
    app/camera_controller.cpp
    app/frame_pipeline.cpp
    app/statistics_controller.cpp
    display/event_display_widget.cpp
    panels/settings_panel.cpp
    panels/information_panel.cpp
    panels/statistics_panel.cpp
    panels/display_panel.cpp
    panels/devices_panel.cpp
    panels/biases_panel.cpp
    panels/roi_panel.cpp
    panels/esp_panel.cpp
    panels/trigger_panel.cpp
    algo_bridge/algo_bridge.cpp
)

add_executable(gui_for_openeb ${GUI_SOURCES})

target_include_directories(gui_for_openeb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(gui_for_openeb PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    gui_algo
)

# Phase 2 panels include HAL facility headers directly
# (metavision/hal/facilities/i_*.h). The HAL target is exported by the
# MetavisionSDK CMake config as MetavisionSDK::hal; link it explicitly so
# the include paths propagate even if gui_algo only links base/core/stream.
if(TARGET MetavisionSDK::hal)
    target_link_libraries(gui_for_openeb PRIVATE MetavisionSDK::hal)
endif()

install(TARGETS gui_for_openeb RUNTIME DESTINATION bin)
