cmake_minimum_required(VERSION 3.16)
project(mt8163_pcm_dsp LANGUAGES CXX)

add_library(mt8163_pcm_dsp STATIC
    src/engine.cpp
)
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
endif()
