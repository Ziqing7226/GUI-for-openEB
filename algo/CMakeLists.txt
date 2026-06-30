# algo/ — self-developed event processing algorithms (C++17, no Qt dependency).
# Linked by gui/ via the gui_algo target.

add_library(gui_algo STATIC
    common/event_buffer.cpp
    common/frame_generator.cpp
    common/data_loader.cpp
)

target_include_directories(gui_algo PUBLIC
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(gui_algo PUBLIC
    MetavisionSDK::base
    MetavisionSDK::core
    MetavisionSDK::stream
    ${OpenCV_LIBS}
)
