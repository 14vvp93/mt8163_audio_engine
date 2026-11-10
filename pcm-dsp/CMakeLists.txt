cmake_minimum_required(VERSION 3.16)
project(mt8163_pcm_dsp LANGUAGES CXX)

add_library(mt8163_pcm_dsp STATIC
    src/engine.cpp
    src/quad_engine.cpp
    src/time_alignment.cpp
    src/crossover.cpp
    src/eq_layout.cpp
    src/multiband_dynamics.cpp
    src/enhancement.cpp
    src/audio_profile.cpp
    src/profiled_quad_engine.cpp
)
set_target_properties(mt8163_pcm_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mt8163_pcm_dsp PUBLIC include)
target_compile_features(mt8163_pcm_dsp PUBLIC cxx_std_17)
target_compile_options(mt8163_pcm_dsp PRIVATE -Wall -Wextra -Wpedantic -Werror)

option(PCM_DSP_BUILD_TESTS "Build host-side tests" ON)
if(PCM_DSP_BUILD_TESTS)
    enable_testing()
    add_executable(pcm_dsp_tests tests/test_main.cpp)
    target_link_libraries(pcm_dsp_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_tests COMMAND pcm_dsp_tests)

    add_executable(pcm_dsp_stability_tests tests/stability_test.cpp)
    target_link_libraries(pcm_dsp_stability_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_stability_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_stability_tests COMMAND pcm_dsp_stability_tests)

    add_executable(pcm_dsp_eq_layout_tests tests/eq_layout_test.cpp)
    target_link_libraries(pcm_dsp_eq_layout_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_eq_layout_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_eq_layout_tests COMMAND pcm_dsp_eq_layout_tests)

    add_executable(pcm_dsp_multiband_dynamics_tests tests/multiband_dynamics_test.cpp)
    target_link_libraries(pcm_dsp_multiband_dynamics_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_multiband_dynamics_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_multiband_dynamics_tests COMMAND pcm_dsp_multiband_dynamics_tests)

    add_executable(pcm_dsp_quad_dynamics_tests tests/quad_dynamics_integration_test.cpp)
    target_link_libraries(pcm_dsp_quad_dynamics_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_quad_dynamics_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_quad_dynamics_tests COMMAND pcm_dsp_quad_dynamics_tests)

    add_executable(pcm_dsp_audio_profile_tests tests/audio_profile_test.cpp)
    target_link_libraries(pcm_dsp_audio_profile_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_audio_profile_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_audio_profile_tests COMMAND pcm_dsp_audio_profile_tests)

    add_executable(pcm_dsp_enhancement_tests tests/enhancement_test.cpp)
    target_link_libraries(pcm_dsp_enhancement_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_enhancement_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_enhancement_tests COMMAND pcm_dsp_enhancement_tests)

    add_executable(pcm_dsp_profile_transition_tests tests/profile_transition_test.cpp)
    target_link_libraries(pcm_dsp_profile_transition_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_profile_transition_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_profile_transition_tests COMMAND pcm_dsp_profile_transition_tests)

    add_executable(pcm_dsp_multiband_quality_tests tests/multiband_dynamics_quality_test.cpp)
    target_link_libraries(pcm_dsp_multiband_quality_tests PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_multiband_quality_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME pcm_dsp_multiband_quality_tests COMMAND pcm_dsp_multiband_quality_tests)
endif()

option(PCM_DSP_BUILD_ANDROID_JNI "Build Android JNI bridge" OFF)
if(ANDROID AND PCM_DSP_BUILD_ANDROID_JNI)
    add_library(mt8163_audio_jni SHARED android/jni_bridge.cpp)
    target_link_libraries(mt8163_audio_jni PRIVATE mt8163_pcm_dsp)
    target_compile_features(mt8163_audio_jni PRIVATE cxx_std_17)
    target_compile_options(mt8163_audio_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(PCM_DSP_BUILD_BENCHMARKS "Build DSP performance benchmark" OFF)
if(PCM_DSP_BUILD_BENCHMARKS)
    add_executable(pcm_dsp_benchmark benchmarks/benchmark_main.cpp)
    target_link_libraries(pcm_dsp_benchmark PRIVATE mt8163_pcm_dsp)
    target_compile_options(pcm_dsp_benchmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
