#include <hardware/audio.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

namespace {

struct FactoryPrimaryDeviceLayout {
    audio_hw_device public_device;
    void* vendor_callbacks[16];
    void* legacy_hardware;
};

static_assert(sizeof(void*) == 4U, "MT8163 Primary HAL must be ARM32");
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

    if (std::strcmp(id, AUDIO_HARDWARE_INTERFACE) != 0) {
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
