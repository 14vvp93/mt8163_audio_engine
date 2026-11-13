cmake_minimum_required(VERSION 3.20)

project(mt8163_android9_abi_layout LANGUAGES C CXX)

enable_testing()

add_executable(android9_abi_layout_test
    tests/android9_abi_layout_test.cpp
)

target_include_directories(android9_abi_layout_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(android9_abi_layout_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(android9_abi_layout_test PRIVATE /W4 /WX)
else()
    target_compile_options(android9_abi_layout_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME android9_abi_layout_test COMMAND android9_abi_layout_test)

add_executable(android9_abi_layout_c_test
    tests/android9_abi_layout_c_test.c
)

target_include_directories(android9_abi_layout_c_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(android9_abi_layout_c_test PRIVATE c_std_11)

if(MSVC)
    target_compile_options(android9_abi_layout_c_test PRIVATE /W4 /WX)
else()
    target_compile_options(android9_abi_layout_c_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME android9_abi_layout_c_test COMMAND android9_abi_layout_c_test)

add_executable(factory_stream_contract_test
    tests/factory_stream_contract_test.cpp
)

target_include_directories(factory_stream_contract_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(factory_stream_contract_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(factory_stream_contract_test PRIVATE /W4 /WX)
else()
    target_compile_options(factory_stream_contract_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME factory_stream_contract_test COMMAND factory_stream_contract_test)
