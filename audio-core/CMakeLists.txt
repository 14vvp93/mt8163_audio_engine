add_library(mt8163_audio_core STATIC
    src/mute_arbiter.cpp
)
target_include_directories(mt8163_audio_core PUBLIC include)
target_compile_features(mt8163_audio_core PUBLIC cxx_std_17)
target_compile_options(mt8163_audio_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(PCM_DSP_BUILD_TESTS)
    add_executable(mt8163_audio_core_tests tests/mute_arbiter_test.cpp)
    target_link_libraries(mt8163_audio_core_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_tests COMMAND mt8163_audio_core_tests)
endif()
