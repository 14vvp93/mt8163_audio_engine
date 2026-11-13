#include <hardware/audio.h>

#include <cerrno>
#include <cstddef>

namespace {

struct FactoryPrimaryDeviceLayout {
    audio_hw_device public_device;
    void* vendor_callbacks[16];
    void* legacy_hardware;
};

static_assert(sizeof(void*) == 4U, "MT8163 Primary HAL must be ARM32");
static_assert(sizeof(audio_module) == 128U, "Android 9 audio_module ABI mismatch");
static_assert(offsetof(hw_module_t, methods) == 20U,
              "Android 9 hw_module_t::methods offset mismatch");
static_assert(offsetof(hw_device_t, close) == 60U,
              "Android 9 hw_device_t::close offset mismatch");
static_assert(offsetof(audio_stream_out, write) == 64U,
              "Android 9 audio_stream_out::write offset mismatch");
static_assert(offsetof(audio_stream_in, read) == 60U,
              "Android 9 audio_stream_in::read offset mismatch");
static_assert(offsetof(audio_hw_device, init_check) == 68U,
              "Android 9 audio_hw_device::init_check offset mismatch");
static_assert(offsetof(audio_hw_device, set_master_mute) == 132U,
              "Android 9 audio_hw_device::set_master_mute offset mismatch");
static_assert(offsetof(audio_hw_device, create_audio_patch) == 140U,
              "Android 9 audio_hw_device::create_audio_patch offset mismatch");
static_assert(offsetof(audio_hw_device, set_audio_port_config) == 152U,
              "Android 9 audio_hw_device::set_audio_port_config offset mismatch");
static_assert(AUDIO_DEVICE_API_VERSION_3_0 == 0x0300,
              "Factory device API version mismatch");
static_assert(sizeof(hw_module_t) == 128U, "Android 9 hw_module_t ABI mismatch");
static_assert(sizeof(hw_device_t) == 64U, "Android 9 hw_device_t ABI mismatch");
static_assert(sizeof(audio_stream) == 56U, "Android 9 audio_stream ABI mismatch");
static_assert(sizeof(audio_stream_out) == 120U,
              "Android 9 audio_stream_out ABI mismatch");
static_assert(sizeof(audio_stream_in) == 96U,
              "Android 9 audio_stream_in ABI mismatch");
static_assert(sizeof(audio_hw_device) == 156U,
              "Android 9 audio_hw_device ABI mismatch");
static_assert(offsetof(FactoryPrimaryDeviceLayout, vendor_callbacks) == 156U,
              "Factory private callback tail offset mismatch");
static_assert(offsetof(FactoryPrimaryDeviceLayout, legacy_hardware) == 220U,
              "Factory legacy hardware pointer offset mismatch");
static_assert(sizeof(FactoryPrimaryDeviceLayout) == 224U,
              "Factory primary device wrapper size mismatch");

bool equals_literal(const char* value, const char* literal) noexcept {
    if (value == nullptr || literal == nullptr) {
        return false;
    }

    while (*value != '\0' && *literal != '\0') {
        if (*value != *literal) {
            return false;
        }
        ++value;
        ++literal;
    }

    return *value == '\0' && *literal == '\0';
}

int open_primary_device(const hw_module_t* module,
                        const char* id,
                        hw_device_t** device) noexcept {
    if (device == nullptr) {
        return -EINVAL;
    }

    *device = nullptr;

    if (module == nullptr || id == nullptr) {
        return -EINVAL;
    }

    if (!equals_literal(id, AUDIO_HARDWARE_INTERFACE)) {
        return -EINVAL;
    }

    // Fail closed until the full device and stream contracts are implemented.
    return -ENOSYS;
}

hw_module_methods_t g_module_methods = {
    open_primary_device,
};

}  // namespace

extern "C" __attribute__((visibility("default")))
struct audio_module HMI = {
    {
        HARDWARE_MODULE_TAG,
        AUDIO_MODULE_API_VERSION_CURRENT,
        HARDWARE_HAL_API_VERSION,
        AUDIO_HARDWARE_MODULE_ID,
        "MTK Audio HW HAL",
        "MTK",
        &g_module_methods,
        nullptr,
        {0},
    },
};
