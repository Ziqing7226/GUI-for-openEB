# algo/ — self-developed event processing algorithms (C++17, no Qt dependency).
# Linked by gui/ via the gui_algo target.

add_library(gui_algo STATIC
    common/event_buffer.cpp
    common/frame_generator.cpp
    common/data_loader.cpp
    calibration/intrinsic.cpp
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

# Optional: ONNX Runtime for E2VID neural-network inference.
# When found, defines GUI_ALGO_HAS_ONNXRUNTIME and links against it.
# Detection: look for the header and shared library in standard paths.
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS /usr/include /usr/local/include /opt/onnxruntime/include
)
find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS /usr/lib /usr/local/lib /opt/onnxruntime/lib
)
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    target_compile_definitions(gui_algo PUBLIC GUI_ALGO_HAS_ONNXRUNTIME)
    target_link_libraries(gui_algo PUBLIC ${ONNXRUNTIME_LIBRARY})
    target_include_directories(gui_algo PUBLIC ${ONNXRUNTIME_INCLUDE_DIR})
    message(STATUS "ONNX Runtime found — E2VID neural-network inference enabled.")
else()
    message(STATUS "ONNX Runtime not found — E2VID will use heuristic fallback.")
endif()


