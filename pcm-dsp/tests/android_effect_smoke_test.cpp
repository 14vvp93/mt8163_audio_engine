#include <hardware/audio_effect.h>
#include <system/audio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void send_command(effect_handle_t handle,
                  std::uint32_t code,
                  std::uint32_t size = 0U,
                  void* data = nullptr,
                  bool has_status_reply = true) {
    std::int32_t status = -999;
    std::uint32_t reply_size = has_status_reply ? sizeof(status) : 0U;
    const std::int32_t transport =
        (*handle)->command(handle, code, size, data, &reply_size,
                           has_status_reply ? &status : nullptr);
    require(transport == 0, "effect command transport status");
    if (has_status_reply) {
        require(reply_size == sizeof(status), "effect command reply size");
        require(status == 0, "effect command reply status");
    } else {
        require(reply_size == 0U, "no-reply command has empty reply");
    }
}

void set_u32(effect_handle_t handle, std::uint32_t id, std::uint32_t value) {
    std::array<std::uint8_t, offsetof(effect_param_t, data) + 8U> bytes{};
    auto* parameter = reinterpret_cast<effect_param_t*>(bytes.data());
    parameter->status = 0;
    parameter->psize = sizeof(id);
    parameter->vsize = sizeof(value);
    std::memcpy(parameter->data, &id, sizeof(id));
    std::memcpy(parameter->data + 4U, &value, sizeof(value));
    send_command(handle, EFFECT_CMD_SET_PARAM,
                 static_cast<std::uint32_t>(bytes.size()), bytes.data());
}

void set_float(effect_handle_t handle, std::uint32_t id, float value) {
    std::array<std::uint8_t, offsetof(effect_param_t, data) + 8U> bytes{};
    auto* parameter = reinterpret_cast<effect_param_t*>(bytes.data());
    parameter->status = 0;
    parameter->psize = sizeof(id);
    parameter->vsize = sizeof(value);
    std::memcpy(parameter->data, &id, sizeof(id));
    std::memcpy(parameter->data + 4U, &value, sizeof(value));
    send_command(handle, EFFECT_CMD_SET_PARAM,
                 static_cast<std::uint32_t>(bytes.size()), bytes.data());
}

effect_config_t make_config(std::uint32_t rate, std::uint8_t format) {
    effect_config_t config{};
    const std::uint32_t mask = EFFECT_CONFIG_SMP_RATE |
                               EFFECT_CONFIG_CHANNELS |
                               EFFECT_CONFIG_FORMAT;
    config.inputCfg.mask = mask;
    config.outputCfg.mask = mask;
    config.inputCfg.samplingRate = rate;
    config.outputCfg.samplingRate = rate;
    config.inputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    config.outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    config.inputCfg.format = format;
    config.outputCfg.format = format;
    config.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    config.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_WRITE;
    return config;
}

void configure(effect_handle_t handle, std::uint32_t rate, std::uint8_t format) {
    send_command(handle, EFFECT_CMD_DISABLE);
    auto config = make_config(rate, format);
    send_command(handle, EFFECT_CMD_SET_CONFIG, sizeof(config), &config);
    send_command(handle, EFFECT_CMD_ENABLE);
}

void set_route(effect_handle_t handle, std::uint32_t device) {
    send_command(handle, EFFECT_CMD_SET_DEVICE, sizeof(device), &device, false);
    std::uint32_t mode = AUDIO_MODE_NORMAL;
    send_command(handle, EFFECT_CMD_SET_AUDIO_MODE, sizeof(mode), &mode, false);
}

} // namespace

int main() {
    effect_uuid_t uuid{
        0x8e5f3c12, 0x7b4d, 0x4a91, 0x9c62,
        {0x1f, 0x08, 0xa7, 0xd3, 0x5e, 0x20}
    };
    effect_descriptor_t descriptor{};
    require(AUDIO_EFFECT_LIBRARY_INFO_SYM.get_descriptor(&uuid, &descriptor) == 0,
            "library returns the custom effect descriptor");
    require((descriptor.flags & EFFECT_FLAG_TYPE_MASK) == EFFECT_FLAG_TYPE_INSERT,
            "descriptor is an AudioFlinger software insert");

    effect_handle_t handle = nullptr;
    require(AUDIO_EFFECT_LIBRARY_INFO_SYM.create_effect(&uuid, 42, 1, &handle) == 0,
            "effect instance creation");
    require(handle != nullptr, "effect handle created");

    send_command(handle, EFFECT_CMD_INIT);
    configure(handle, 48000U, AUDIO_FORMAT_PCM_16_BIT);
    set_route(handle, AUDIO_DEVICE_OUT_SPEAKER);

    std::vector<std::int16_t> input(128U, 1000);
    std::vector<std::int16_t> output(128U, 0);
    audio_buffer_t in_buffer{};
    audio_buffer_t out_buffer{};
    in_buffer.raw = input.data();
    out_buffer.raw = output.data();
    in_buffer.frameCount = 64U;
    out_buffer.frameCount = 64U;

    // Framework attachment alone must not activate DSP processing.
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "default-disabled DSP bypass succeeds");
    require(output == input, "default effect state is bit-exact bypass");

    set_u32(handle, 1U, 1U); // DSP enabled
    set_float(handle, 2U, 6.0F); // +6 dB preamp
    std::fill(output.begin(), output.end(), 0);
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "PCM16 stereo process succeeds");
    require(output[0] > 1500 && output[0] < 2600,
            "PCM16 preamp is applied in the software insert");

    // Explicit DSP-off must be bit-exact bypass.
    set_u32(handle, 1U, 0U);
    std::fill(output.begin(), output.end(), 0);
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "disabled DSP bypass succeeds");
    require(output == input, "disabled DSP bypass is bit-exact");

    // Unsupported sample rate and unsupported route must fail open.
    set_u32(handle, 1U, 1U);
    configure(handle, 32000U, AUDIO_FORMAT_PCM_16_BIT);
    std::fill(output.begin(), output.end(), 0);
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "unsupported sample-rate bypass succeeds");
    require(output == input, "unsupported sample-rate bypass is bit-exact");

    // The recovered vendor A2DP policy exposes PCM16 stereo at 44.1 kHz.
    // All three A2DP device masks must enter the same software DSP path.
    configure(handle, 44100U, AUDIO_FORMAT_PCM_16_BIT);
    set_u32(handle, 1U, 1U);
    set_float(handle, 2U, 6.0F);
    const std::array<std::uint32_t, 3> a2dp_devices{{
        AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
        AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES,
        AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER
    }};
    for (const auto device : a2dp_devices) {
        set_route(handle, device);
        std::fill(output.begin(), output.end(), 0);
        require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
                "A2DP PCM DSP process succeeds");
        require(output[0] > 1500 && output[0] < 2600,
                "A2DP PCM route applies the software preamp");
    }

    // Bluetooth SCO is a telephony route and must remain bypassed.
    set_route(handle, AUDIO_DEVICE_OUT_BLUETOOTH_SCO);
    std::fill(output.begin(), output.end(), 0);
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "Bluetooth SCO bypass succeeds");
    require(output == input, "Bluetooth SCO bypass is bit-exact");

    // Offload and non-normal modes must also bypass bit-exactly.
    configure(handle, 48000U, AUDIO_FORMAT_PCM_16_BIT);
    set_route(handle, AUDIO_DEVICE_OUT_SPEAKER);
    set_u32(handle, 1U, 1U);
    effect_offload_param_t offload{};
    offload.isOffload = true;
    send_command(handle, EFFECT_CMD_OFFLOAD, sizeof(offload), &offload);
    std::fill(output.begin(), output.end(), 0);
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "offload bypass succeeds");
    require(output == input, "offload bypass is bit-exact");
    offload.isOffload = false;
    send_command(handle, EFFECT_CMD_OFFLOAD, sizeof(offload), &offload);
    std::uint32_t in_call_mode = AUDIO_MODE_IN_CALL;
    send_command(handle, EFFECT_CMD_SET_AUDIO_MODE, sizeof(in_call_mode),
                 &in_call_mode, false);
    std::fill(output.begin(), output.end(), 0);
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "in-call mode bypass succeeds");
    require(output == input, "in-call mode bypass is bit-exact");
    std::uint32_t normal_mode = AUDIO_MODE_NORMAL;
    send_command(handle, EFFECT_CMD_SET_AUDIO_MODE, sizeof(normal_mode),
                 &normal_mode, false);

    // Float32 adapter uses the same stereo core and fixed-size scratch.
    configure(handle, 48000U, AUDIO_FORMAT_PCM_FLOAT);
    set_route(handle, AUDIO_DEVICE_OUT_SPEAKER);
    set_u32(handle, 1U, 1U);
    set_float(handle, 2U, 6.0F);
    std::vector<float> float_input(128U, 0.1F);
    std::vector<float> float_output(128U, 0.0F);
    in_buffer.raw = float_input.data();
    out_buffer.raw = float_output.data();
    require((*handle)->process(handle, &in_buffer, &out_buffer) == 0,
            "Float32 stereo process succeeds");
    require(float_output[0] > 0.17F && float_output[0] < 0.23F,
            "Float32 adapter applies preamp");

    send_command(handle, EFFECT_CMD_DISABLE);
    require(AUDIO_EFFECT_LIBRARY_INFO_SYM.release_effect(handle) == 0,
            "effect instance release");
    std::puts("Android effect ABI smoke tests passed");
    return 0;
}
