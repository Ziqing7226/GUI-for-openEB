# algo/tests/ — GTest-based unit tests for self-developed algorithm modules.

find_package(GTest REQUIRED)

# Test executable for Phase 6 (algo/common/) modules.
add_executable(test_phase6_common
    test_phase6_common.cpp
)

target_link_libraries(test_phase6_common PRIVATE
    gui_algo
    GTest::GTest
    GTest::Main
)

target_compile_options(test_phase6_common PRIVATE -Wall -Wextra -Werror)

include(GoogleTest)
gtest_discover_tests(test_phase6_common)

# Test executable for Phase 7-9 (algo/cv/) modules.
# Depends on algo/cv/ headers (noise_filter, hot_pixel_filter, orientation_filter,
# direction_selective_filter, sparse_optical_flow, blob_detector, object_tracker,
# corner_detector, cluster_interface, cluster_path_point).
add_executable(test_phase7_cv
    test_phase7_cv.cpp
)

target_link_libraries(test_phase7_cv PRIVATE
    gui_algo
    GTest::GTest
    GTest::Main
)

target_compile_options(test_phase7_cv PRIVATE -Wall -Wextra -Werror)

gtest_discover_tests(test_phase7_cv)

# Test executable for Phase 8-10 (algo/cv/ + algo/analytics/) modules.
# Depends on algo/cv/ and algo/analytics/ headers (line_segment_detector,
# hough_*, orientation_cluster, cluster_lif, background_mask_filter,
# perspective_undistort, trigger_synced_filter, bandpass_filter, optical_gyro,
# ultra_slow_motion, xyt_visualizer, overlay, time_surface, active_marker,
# event_to_video, flow_statistics, isi_analyzer, particle_counter,
# auto_bias_controller, freq_detector).
add_executable(test_phase8_10
    test_phase8_10.cpp
)

target_link_libraries(test_phase8_10 PRIVATE
    gui_algo
    GTest::GTest
    GTest::Main
)

target_compile_options(test_phase8_10 PRIVATE -Wall -Wextra -Werror)

gtest_discover_tests(test_phase8_10)
