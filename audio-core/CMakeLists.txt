add_library(mt8163_audio_core STATIC
    src/mute_arbiter.cpp
    src/cd3313_compat.cpp
    src/session_registry.cpp
    src/route_policy.cpp
    src/audio_core.cpp
    src/hardware_broker.cpp
    src/pcm_backend_contract.cpp
    src/endpoint_pcm_backend.cpp
    src/pcm_stream_broker.cpp
    src/snapshot_publisher_compile_check.cpp
)
target_include_directories(mt8163_audio_core PUBLIC include)
target_compile_features(mt8163_audio_core PUBLIC cxx_std_17)
target_compile_options(mt8163_audio_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(PCM_DSP_BUILD_TESTS)
    add_executable(mt8163_audio_core_tests tests/mute_arbiter_test.cpp)
    target_link_libraries(mt8163_audio_core_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_tests COMMAND mt8163_audio_core_tests)

    add_executable(mt8163_audio_core_coordinator_tests tests/audio_core_test.cpp)
    target_link_libraries(mt8163_audio_core_coordinator_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_coordinator_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_coordinator_tests COMMAND mt8163_audio_core_coordinator_tests)

    add_executable(mt8163_audio_core_route_tests tests/route_policy_test.cpp)
    target_link_libraries(mt8163_audio_core_route_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_route_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_route_tests COMMAND mt8163_audio_core_route_tests)

    add_executable(mt8163_audio_core_hardware_tests tests/hardware_broker_test.cpp)
    target_link_libraries(mt8163_audio_core_hardware_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_hardware_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_hardware_tests COMMAND mt8163_audio_core_hardware_tests)

    add_executable(mt8163_audio_core_session_tests tests/session_registry_test.cpp)
    target_link_libraries(mt8163_audio_core_session_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_session_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_session_tests COMMAND mt8163_audio_core_session_tests)

    add_executable(mt8163_audio_core_pcm_stream_tests tests/pcm_stream_broker_test.cpp)
    target_link_libraries(mt8163_audio_core_pcm_stream_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_pcm_stream_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_pcm_stream_tests COMMAND mt8163_audio_core_pcm_stream_tests)

    add_executable(mt8163_audio_core_pcm_contract_tests tests/pcm_backend_contract_test.cpp)
    target_link_libraries(mt8163_audio_core_pcm_contract_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_pcm_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_pcm_contract_tests COMMAND mt8163_audio_core_pcm_contract_tests)

    add_executable(mt8163_audio_core_endpoint_backend_tests tests/endpoint_pcm_backend_test.cpp)
    target_link_libraries(mt8163_audio_core_endpoint_backend_tests PRIVATE mt8163_audio_core)
    target_compile_options(mt8163_audio_core_endpoint_backend_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_endpoint_backend_tests COMMAND mt8163_audio_core_endpoint_backend_tests)

    find_package(Threads REQUIRED)
    add_executable(mt8163_audio_core_snapshot_tests tests/snapshot_publisher_test.cpp)
    target_link_libraries(mt8163_audio_core_snapshot_tests PRIVATE mt8163_audio_core Threads::Threads)
    target_compile_options(mt8163_audio_core_snapshot_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME mt8163_audio_core_snapshot_tests COMMAND mt8163_audio_core_snapshot_tests)
endif()
