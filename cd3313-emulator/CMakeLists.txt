cmake_minimum_required(VERSION 3.16)
project(cd3313_emulator LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(cd3313_protocol
    src/protocol.cpp
    src/emulator.cpp
    src/channel_control.cpp
)
target_include_directories(cd3313_protocol PUBLIC include)
target_compile_options(cd3313_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(cd3313_tests tests/test_main.cpp)
target_link_libraries(cd3313_tests PRIVATE cd3313_protocol)
target_compile_options(cd3313_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(cd3313_protocol_boundary_tests tests/protocol_boundary_test.cpp)
target_link_libraries(cd3313_protocol_boundary_tests PRIVATE cd3313_protocol)
target_compile_options(cd3313_protocol_boundary_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
enable_testing()
add_test(NAME cd3313_tests COMMAND cd3313_tests)
add_test(NAME cd3313_protocol_boundary_tests COMMAND cd3313_protocol_boundary_tests)

option(CD3313_BUILD_FUZZER "Build the libFuzzer protocol target (Clang only)" OFF)
if(CD3313_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CD3313_BUILD_FUZZER requires Clang with libFuzzer")
    endif()
    target_compile_options(cd3313_protocol PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
    )
    add_executable(cd3313_protocol_fuzzer fuzz/protocol_fuzz.cpp)
    target_link_libraries(cd3313_protocol_fuzzer PRIVATE cd3313_protocol)
    target_compile_options(cd3313_protocol_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -Wall -Wextra -Wpedantic -Werror
    )
    target_link_options(cd3313_protocol_fuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
endif()
