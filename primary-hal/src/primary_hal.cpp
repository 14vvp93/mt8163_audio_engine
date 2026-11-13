#include <hardware/audio.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <new>

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
              "Android 9 audio_hw_device ABI mismatch");
static_assert(offsetof(audio_hw_device, set_master_mute) == 132U,
              "Android 9 audio_hw_device ABI mismatch");
static_assert(offsetof(audio_hw_device, create_audio_patch) == 140U,
              "Android 9 audio_hw_device ABI mismatch");
static_assert(offsetof(audio_hw_device, set_audio_port_config) == 152U,
              "Android 9 audio_hw_device ABI mismatch");
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
    if (value == nullptr || literal == nullptr) return false;
    while (*value != '\0' && *literal != '\0') {
        if (*value != *literal) return false;
        ++value;
        ++literal;
    }
    return *value == '\0' && *literal == '\0';
}

int close_device(hw_device_t* common) noexcept {
    if (common == nullptr) return -EINVAL;
    auto* device = reinterpret_cast<FactoryPrimaryDeviceLayout*>(common);
    delete device;
    return 0;
}

int init_check(const audio_hw_device*) noexcept { return -ENODEV; }
uint32_t get_supported_devices(const audio_hw_device*) noexcept { return 0U; }

int set_voice_volume(audio_hw_device*, float volume) noexcept {
    return volume < 0.0F || volume > 1.0F ? -EINVAL : -ENODEV;
}
int set_master_volume(audio_hw_device*, float volume) noexcept {
    return volume < 0.0F || volume > 1.0F ? -EINVAL : -ENOSYS;
}
int set_mode(audio_hw_device*, audio_mode_t mode) noexcept {
    if (mode != AUDIO_MODE_NORMAL && mode != AUDIO_MODE_RINGTONE &&
        mode != AUDIO_MODE_IN_CALL && mode != AUDIO_MODE_IN_COMMUNICATION) return -EINVAL;
    return -ENODEV;
}
int set_mic_mute(audio_hw_device*, bool) noexcept { return -ENODEV; }
int get_mic_mute(const audio_hw_device*, bool* state) noexcept {
    if (state == nullptr) return -EINVAL;
    *state = false;
    return -ENODEV;
}
int set_device_parameters(audio_hw_device*, const char* parameters) noexcept {
    return parameters == nullptr ? -EINVAL : -ENOSYS;
}
char* get_device_parameters(const audio_hw_device*, const char*) noexcept {
    auto* result = static_cast<char*>(std::malloc(1U));
    if (result != nullptr) result[0] = '\0';
    return result;
}
size_t get_input_buffer_size(const audio_hw_device*, const audio_config*) noexcept {
    return 0U;
}

int open_output_stream(audio_hw_device*, audio_io_handle_t, audio_devices_t,
                       audio_output_flags_t, audio_config*,
                       audio_stream_out** stream, const char*) noexcept {
    if (stream == nullptr) return -EINVAL;
    *stream = nullptr;
    return -ENODEV;
}
void close_output_stream(audio_hw_device*, audio_stream_out*) noexcept {}

int open_input_stream(audio_hw_device*, audio_io_handle_t, audio_devices_t,
                      audio_config*, audio_stream_in** stream,
                      audio_input_flags_t, const char*, audio_source_t) noexcept {
    if (stream == nullptr) return -EINVAL;
    *stream = nullptr;
    return -ENODEV;
}
void close_input_stream(audio_hw_device*, audio_stream_in*) noexcept {}

int get_microphones(const audio_hw_device*, audio_microphone_characteristic_t*,
                    size_t* count) noexcept {
    if (count == nullptr) return -EINVAL;
    *count = 0U;
    return -ENOSYS;
}
int dump_device(const audio_hw_device*, int) noexcept { return -ENOSYS; }
int set_master_mute(audio_hw_device*, bool) noexcept { return -ENOSYS; }
int get_master_mute(audio_hw_device*, bool* mute) noexcept {
    if (mute == nullptr) return -EINVAL;
    *mute = false;
    return -ENOSYS;
}
int create_audio_patch(audio_hw_device*, unsigned int, const audio_port_config*,
                       unsigned int, const audio_port_config*,
                       audio_patch_handle_t*) noexcept { return -ENOSYS; }
int release_audio_patch(audio_hw_device*, audio_patch_handle_t) noexcept { return -ENOSYS; }
int get_audio_port(audio_hw_device*, audio_port*) noexcept { return -ENOSYS; }
int set_audio_port_config(audio_hw_device*, const audio_port_config*) noexcept {
    return -ENOSYS;
}

int open_primary_device(const hw_module_t* module, const char* id,
                        hw_device_t** device) noexcept {
    if (device == nullptr) return -EINVAL;
    *device = nullptr;
    if (module == nullptr || id == nullptr ||
        !equals_literal(id, AUDIO_HARDWARE_INTERFACE)) return -EINVAL;

    auto* instance = new (std::nothrow) FactoryPrimaryDeviceLayout{};
    if (instance == nullptr) return -ENOMEM;

    audio_hw_device& audio = instance->public_device;
    audio.common.tag = HARDWARE_DEVICE_TAG;
    audio.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    audio.common.module = const_cast<hw_module_t*>(module);
    audio.common.close = close_device;

    audio.get_supported_devices = get_supported_devices;
    audio.init_check = init_check;
    audio.set_voice_volume = set_voice_volume;
    audio.set_master_volume = set_master_volume;
    audio.get_master_volume = nullptr;
    audio.set_mode = set_mode;
    audio.set_mic_mute = set_mic_mute;
    audio.get_mic_mute = get_mic_mute;
    audio.set_parameters = set_device_parameters;
    audio.get_parameters = get_device_parameters;
    audio.get_input_buffer_size = get_input_buffer_size;
    audio.open_output_stream = open_output_stream;
    audio.close_output_stream = close_output_stream;
    audio.open_input_stream = open_input_stream;
    audio.close_input_stream = close_input_stream;
    audio.get_microphones = get_microphones;
    audio.dump = dump_device;
    audio.set_master_mute = set_master_mute;
    audio.get_master_mute = get_master_mute;
    audio.create_audio_patch = create_audio_patch;
    audio.release_audio_patch = release_audio_patch;
    audio.get_audio_port = get_audio_port;
    audio.set_audio_port_config = set_audio_port_config;

    *device = &audio.common;
    return 0;
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
