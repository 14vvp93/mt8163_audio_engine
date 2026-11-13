#include "mt8163_android9_abi_layout32.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

int main() {
    std::cout << "hw_module_t=" << sizeof(mt8163_hw_module_layout32_t) << '\n';
    std::cout << "hw_device_t=" << sizeof(mt8163_hw_device_layout32_t) << '\n';
    std::cout << "audio_stream_out=" << sizeof(mt8163_audio_stream_out_layout32_t) << '\n';
    std::cout << "audio_stream_in=" << sizeof(mt8163_audio_stream_in_layout32_t) << '\n';
    std::cout << "audio_hw_device=" << sizeof(mt8163_audio_hw_device_layout32_t) << '\n';
    std::cout << "factory_audio_hw_device="
              << sizeof(mt8163_factory_audio_hw_device_layout32_t) << '\n';
    std::cout << "factory_stream_out="
              << sizeof(mt8163_factory_stream_out_layout32_t) << '\n';
    std::cout << "factory_stream_in="
              << sizeof(mt8163_factory_stream_in_layout32_t) << '\n';
    return 0;
}
