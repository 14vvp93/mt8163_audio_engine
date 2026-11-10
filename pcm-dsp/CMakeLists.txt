cmake_minimum_required(VERSION 3.16)
project(mt8163_pcm_dsp LANGUAGES CXX)

add_library(mt8163_pcm_dsp STATIC
    src/engine.cpp
    src/quad_engine.cpp
    src/time_alignment.cpp
    src/crossover.cpp
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
