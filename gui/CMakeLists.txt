# gui/ — Qt 6 GUI application for openEB.

set(GUI_SOURCES
    main.cpp
    main_window.cpp
    app/camera_controller.cpp
    app/file_converter.cpp
    app/frame_pipeline.cpp
    app/statistics_controller.cpp
    algo_bridge/algo_bridge.cpp
    algo_bridge/algo_backend.cpp
    algo_bridge/filter_chain.cpp
    config/config_manager.cpp
    display/event_display_widget.cpp
    exporter/exporter_controller.cpp
    exporter/export_dialog.cpp
    panels/algorithms_panel.cpp
    panels/biases_panel.cpp
    panels/devices_panel.cpp
    panels/display_panel.cpp
    panels/esp_panel.cpp
    panels/file_tools_panel.cpp
    panels/information_panel.cpp
    panels/preprocessing_panel.cpp
    panels/roi_panel.cpp
    panels/settings_panel.cpp
    panels/statistics_panel.cpp
    panels/trigger_panel.cpp
    recorder/playback_controller.cpp
    recorder/playback_controls.cpp
    recorder/recorder_controller.cpp
)

# Phase 9 — calibration wizard (built when sources are present).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/calibration/calibration_wizard.cpp")
    list(APPEND GUI_SOURCES
        calibration/calibration_wizard.cpp
    )
endif()

# Phase 10 — temporal plot / multi-window / layout manager.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/temporal/temporal_plot_window.cpp")
    list(APPEND GUI_SOURCES
        temporal/temporal_plot_window.cpp
        widgets/multi_window_manager.cpp
        config/layout_manager.cpp
    )
endif()

# Phase 11 — jAER-inspired display / widgets / bias-tweaks (design §1.6).
# Header-only utilities (multiline_text.h, adaptive_renderer.h,
# contrast_controller.h) need no source entries.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/display/space_time_display.cpp")
    list(APPEND GUI_SOURCES
        widgets/target_labeler.cpp
        widgets/pixel_probe.cpp
        widgets/mouse_adaptor.cpp
        widgets/algo_window.cpp
        display/frame_annotator.cpp
        display/space_time_display.cpp
        panels/bias_tweaks_panel.cpp
    )
endif()

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
