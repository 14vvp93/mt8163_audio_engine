#include "pcm_dsp/engine.hpp"

#include <hardware/audio_effect.h>
#include <system/audio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr effect_uuid_t kEffectTypeEqualizer = {
    0x0bed4300, 0xddd6, 0x11db, 0x8f34,
    {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}
};
constexpr effect_uuid_t kEffectUuid = {
    0x8e5f3c12, 0x7b4d, 0x4a91, 0x9c62,
    {0x1f, 0x08, 0xa7, 0xd3, 0x5e, 0x20}
};

constexpr std::size_t kMaxFloatFrames = 4096U;
constexpr std::uint32_t kWriterBit = 0x80000000U;
constexpr std::uint32_t kReaderMask = ~kWriterBit;

enum ParameterId : std::uint32_t {
    kParamEnabled = 1U,
    kParamPreampDb = 2U,
    kParamBalance = 3U,
    kParamLimiterEnabled = 4U,
    kParamLimiterThreshold = 5U,
    kParamBandBase = 100U
};

struct BandParameter {
    float frequency_hz;
    float gain_db;
    float q;
    std::uint32_t enabled;
    std::uint32_t type;
    float shelf_slope;
};

struct ParameterSlot {
    std::atomic<std::uint32_t> state{0U};
    pcm_dsp::Parameters value{};
};

struct EffectContext {
    effect_interface_s* interface;
    effect_config_t config{};
    pcm_dsp::Engine engine;
    pcm_dsp::Parameters control_parameters{};
    pcm_dsp::Parameters deferred_parameters{};
    bool has_deferred_parameters{false};

    std::array<ParameterSlot, 3> slots{};
    std::atomic<int> published_slot{0};
    std::atomic<bool> framework_enabled{false};
    std::atomic<std::uint32_t> active_process_calls{0U};
    std::atomic<bool> configured{false};
    std::atomic<bool> supported_config{false};
    std::atomic<bool> bypass_copy_compatible{false};
    std::atomic<bool> reset_requested{false};
    std::atomic<std::uint32_t> output_device{0U};
    std::atomic<std::uint32_t> audio_mode{AUDIO_MODE_NORMAL};
    std::atomic<bool> offload{false};

    // Scratch is fixed-size and owned by the effect instance. It is used only
    // by the audio thread for Float32 <-> PCM16 conversion.
    std::array<std::int16_t, kMaxFloatFrames * 2U> float_input{};
    std::array<std::int16_t, kMaxFloatFrames * 2U> float_output{};
    bool was_processing{false};

    explicit EffectContext(effect_interface_s* itf) : interface(itf) {
        slots[0].value = control_parameters;
    }
};

bool uuid_equal(const effect_uuid_t& a, const effect_uuid_t& b) {
    return std::memcmp(&a, &b, sizeof(effect_uuid_t)) == 0;
}

EffectContext* context_from_handle(effect_handle_t handle) {
    return reinterpret_cast<EffectContext*>(handle);
}

bool acquire_parameters(EffectContext* context, int* slot_index) {
    for (unsigned attempt = 0; attempt < 4U; ++attempt) {
        const int index = context->published_slot.load(std::memory_order_acquire);
        if (index < 0 || index >= static_cast<int>(context->slots.size())) {
            return false;
        }
        auto& slot = context->slots[static_cast<std::size_t>(index)];
        std::uint32_t state = slot.state.load(std::memory_order_relaxed);
        if ((state & kWriterBit) != 0U || (state & kReaderMask) == kReaderMask) {
            continue;
        }
        if (!slot.state.compare_exchange_weak(
                state, state + 1U, std::memory_order_acquire,
                std::memory_order_relaxed)) {
            continue;
        }
        if (context->published_slot.load(std::memory_order_acquire) == index) {
            *slot_index = index;
            return true;
        }
        slot.state.fetch_sub(1U, std::memory_order_release);
    }
    return false;
}

void release_parameters(EffectContext* context, int index) {
    context->slots[static_cast<std::size_t>(index)].state.fetch_sub(
        1U, std::memory_order_release);
}

bool publish_parameters(EffectContext* context,
                        const pcm_dsp::Parameters& parameters) {
    const int active = context->published_slot.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < context->slots.size(); ++i) {
        if (static_cast<int>(i) == active) continue;
        std::uint32_t expected = 0U;
        auto& slot = context->slots[i];
        if (!slot.state.compare_exchange_strong(
                expected, kWriterBit, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            continue;
        }
        slot.value = parameters;
        slot.state.store(0U, std::memory_order_release);
        context->published_slot.store(static_cast<int>(i),
                                      std::memory_order_release);
        return true;
    }
    return false;
}

bool supported_device(std::uint32_t device) {
    constexpr std::uint32_t allowed =
        static_cast<std::uint32_t>(AUDIO_DEVICE_OUT_SPEAKER) |
        static_cast<std::uint32_t>(AUDIO_DEVICE_OUT_WIRED_HEADSET) |
        static_cast<std::uint32_t>(AUDIO_DEVICE_OUT_WIRED_HEADPHONE);
    return device != 0U && (device & ~allowed) == 0U;
}

std::size_t bytes_per_sample(std::uint8_t format);

bool supported_format(std::uint8_t format) {
    return format == AUDIO_FORMAT_PCM_16_BIT ||
           format == AUDIO_FORMAT_PCM_FLOAT;
}

bool configure_effect(EffectContext* context, const effect_config_t& config) {
    if (context->framework_enabled.load(std::memory_order_acquire) ||
        context->active_process_calls.load(std::memory_order_acquire) != 0U) {
        return false;
    }
    context->config = config;
    const auto& in = config.inputCfg;
    const auto& out = config.outputCfg;

    const bool fields_present =
        (in.mask & (EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS |
                    EFFECT_CONFIG_FORMAT)) ==
            (EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS |
             EFFECT_CONFIG_FORMAT) &&
        (out.mask & (EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS |
                     EFFECT_CONFIG_FORMAT)) ==
            (EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS |
             EFFECT_CONFIG_FORMAT);

    const bool same_layout = fields_present &&
        in.channels == out.channels && in.format == out.format;
    context->bypass_copy_compatible.store(same_layout &&
        audio_channel_count_from_out_mask(
            static_cast<audio_channel_mask_t>(out.channels)) > 0U &&
        bytes_per_sample(out.format) > 0U, std::memory_order_release);
    const bool same_stream = same_layout &&
        in.samplingRate == out.samplingRate &&
        in.channels == AUDIO_CHANNEL_OUT_STEREO &&
        out.channels == AUDIO_CHANNEL_OUT_STEREO &&
        supported_format(in.format);

    bool supported = false;
    if (same_stream &&
        (in.samplingRate == 44100U || in.samplingRate == 48000U)) {
        supported = context->engine.configure(in.samplingRate);
    }

    context->supported_config.store(supported, std::memory_order_release);
    context->configured.store(true, std::memory_order_release);
    context->reset_requested.store(true, std::memory_order_release);
    return true; // Unsupported but valid streams are deliberately bypassed.
}

std::size_t bytes_per_sample(std::uint8_t format) {
    switch (format) {
        case AUDIO_FORMAT_PCM_8_BIT: return 1U;
        case AUDIO_FORMAT_PCM_16_BIT: return 2U;
        case AUDIO_FORMAT_PCM_24_BIT_PACKED: return 3U;
        case AUDIO_FORMAT_PCM_8_24_BIT:
        case AUDIO_FORMAT_PCM_32_BIT:
        case AUDIO_FORMAT_PCM_FLOAT: return 4U;
        default: return 0U;
    }
}

void copy_bypass(const EffectContext* context,
                 const audio_buffer_t& input,
                 audio_buffer_t& output,
                 std::uint8_t format) {
    if (input.raw == nullptr || output.raw == nullptr ||
        input.raw == output.raw || input.frameCount == 0U) {
        return;
    }
    const std::size_t sample_bytes = bytes_per_sample(format);
    const std::size_t channels =
        audio_channel_count_from_out_mask(
            static_cast<audio_channel_mask_t>(context->config.outputCfg.channels));
    if (sample_bytes == 0U || channels == 0U) return;
    const std::size_t bytes = input.frameCount * channels * sample_bytes;
    std::memmove(output.raw, input.raw, bytes);
}

std::int32_t process_effect(effect_handle_t self,
                            audio_buffer_t* in_buffer,
                            audio_buffer_t* out_buffer) {
    auto* context = context_from_handle(self);
    if (context == nullptr) return -EINVAL;
    context->active_process_calls.fetch_add(1U, std::memory_order_acq_rel);
    struct CallbackGuard {
        EffectContext* context;
        ~CallbackGuard() {
            context->active_process_calls.fetch_sub(1U, std::memory_order_release);
        }
    } callback_guard{context};
    (void)callback_guard;
    if (!context->framework_enabled.load(std::memory_order_acquire)) {
        return -ENODATA;
    }
    if (!context->configured.load(std::memory_order_acquire)) return -EINVAL;

    audio_buffer_t input = in_buffer != nullptr
        ? *in_buffer : context->config.inputCfg.buffer;
    audio_buffer_t output = out_buffer != nullptr
        ? *out_buffer : context->config.outputCfg.buffer;
    if (input.raw == nullptr || output.raw == nullptr ||
        input.frameCount != output.frameCount) {
        return -EINVAL;
    }
    if (!context->bypass_copy_compatible.load(std::memory_order_acquire)) return -EINVAL;

    const std::uint8_t format = context->config.outputCfg.format;
    const bool route_allowed =
        context->audio_mode.load(std::memory_order_acquire) == AUDIO_MODE_NORMAL &&
        supported_device(context->output_device.load(std::memory_order_acquire)) &&
        !context->offload.load(std::memory_order_acquire) &&
        context->supported_config.load(std::memory_order_acquire);

    int slot_index = -1;
    if (!route_allowed || !acquire_parameters(context, &slot_index)) {
        if (context->was_processing) {
            context->engine.reset();
            context->was_processing = false;
        }
        copy_bypass(context, input, output, format);
        return 0;
    }

    const auto& parameters =
        context->slots[static_cast<std::size_t>(slot_index)].value;
    if (!parameters.enabled) {
        if (context->was_processing) {
            context->engine.reset();
            context->was_processing = false;
        }
        copy_bypass(context, input, output, format);
        release_parameters(context, slot_index);
        return 0;
    }

    if (context->reset_requested.exchange(false, std::memory_order_acq_rel)) {
        context->engine.reset();
    }

    bool ok = true;
    if (format == AUDIO_FORMAT_PCM_16_BIT) {
        ok = context->engine.process(input.s16, output.s16,
                                     input.frameCount, parameters);
    } else if (format == AUDIO_FORMAT_PCM_FLOAT) {
        std::size_t offset = 0U;
        while (offset < input.frameCount && ok) {
            const std::size_t frames = std::min(
                kMaxFloatFrames, input.frameCount - offset);
            const std::size_t samples = frames * 2U;
            const float* source = input.raw != nullptr
                ? static_cast<const float*>(input.raw) + offset * 2U : nullptr;
            float* destination = static_cast<float*>(output.raw) + offset * 2U;
            for (std::size_t i = 0; i < samples; ++i) {
                const float value = std::isfinite(source[i])
                    ? std::max(-1.0F, std::min(source[i], 1.0F)) : 0.0F;
                context->float_input[i] = static_cast<std::int16_t>(
                    std::lrint(value * 32767.0F));
            }
            ok = context->engine.process(context->float_input.data(),
                                         context->float_output.data(),
                                         frames, parameters);
            if (ok) {
                for (std::size_t i = 0; i < samples; ++i) {
                    destination[i] =
                        static_cast<float>(context->float_output[i]) / 32768.0F;
                }
            }
            offset += frames;
        }
    } else {
        ok = false;
    }

    context->was_processing = ok;
    release_parameters(context, slot_index);
    if (!ok) {
        copy_bypass(input, output, format);
        context->engine.reset();
        context->was_processing = false;
    }
    return 0;
}

void write_status(uint32_t* reply_size, void* reply_data, std::int32_t status) {
    if (reply_size == nullptr) return;
    if (reply_data != nullptr && *reply_size >= sizeof(status)) {
        std::memcpy(reply_data, &status, sizeof(status));
        *reply_size = sizeof(status);
    } else {
        *reply_size = 0U;
    }
}


std::int32_t apply_parameter(EffectContext* context,
                             std::uint32_t id,
                             const void* value,
                             std::uint32_t value_size,
                             bool deferred) {
    pcm_dsp::Parameters candidate = deferred && context->has_deferred_parameters
        ? context->deferred_parameters : context->control_parameters;

    auto read_u32 = [&]() {
        std::uint32_t result = 0U;
        std::memcpy(&result, value, sizeof(result));
        return result;
    };
    auto read_float = [&]() {
        float result = 0.0F;
        std::memcpy(&result, value, sizeof(result));
        return result;
    };

    switch (id) {
        case kParamEnabled:
            if (value_size != sizeof(std::uint32_t)) return -EINVAL;
            candidate.enabled = read_u32() != 0U;
            break;
        case kParamPreampDb:
            if (value_size != sizeof(float)) return -EINVAL;
            candidate.preamp_db = read_float();
            break;
        case kParamBalance:
            if (value_size != sizeof(float)) return -EINVAL;
            candidate.balance = read_float();
            break;
        case kParamLimiterEnabled:
            if (value_size != sizeof(std::uint32_t)) return -EINVAL;
            candidate.limiter_enabled = read_u32() != 0U;
            break;
        case kParamLimiterThreshold:
            if (value_size != sizeof(float)) return -EINVAL;
            candidate.limiter_threshold = read_float();
            break;
        default:
            if (id < kParamBandBase ||
                id >= kParamBandBase + pcm_dsp::kEqBandCount ||
                value_size != sizeof(BandParameter)) {
                return -EINVAL;
            }
            {
                BandParameter band{};
                std::memcpy(&band, value, sizeof(band));
                if (band.type > static_cast<std::uint32_t>(
                        pcm_dsp::FilterType::LowPass)) return -EINVAL;
                auto& target = candidate.bands[id - kParamBandBase];
                target.frequency_hz = band.frequency_hz;
                target.gain_db = band.gain_db;
                target.q = band.q;
                target.enabled = band.enabled != 0U;
                target.type = static_cast<pcm_dsp::FilterType>(band.type);
                target.shelf_slope = band.shelf_slope;
            }
            break;
    }

    const auto finite = [](float value) { return std::isfinite(value); };
    if (!finite(candidate.preamp_db) || !finite(candidate.balance) ||
        !finite(candidate.limiter_threshold) ||
        !finite(candidate.limiter_attack_ms) ||
        !finite(candidate.limiter_release_ms)) {
        return -EINVAL;
    }
    for (const auto& band : candidate.bands) {
        if (!finite(band.frequency_hz) || !finite(band.gain_db) ||
            !finite(band.q) || !finite(band.shelf_slope)) {
            return -EINVAL;
        }
    }

    if (deferred) {
        context->deferred_parameters = candidate;
        context->has_deferred_parameters = true;
        return 0;
    }
    if (!publish_parameters(context, candidate)) return -ENOSPC;
    context->control_parameters = candidate;
    return 0;
}

std::int32_t get_parameter_value(const pcm_dsp::Parameters& parameters,
                                 std::uint32_t id,
                                 void* value,
                                 std::uint32_t* value_size) {
    if (value == nullptr || value_size == nullptr) return -EINVAL;
    switch (id) {
        case kParamEnabled: {
            const std::uint32_t result = parameters.enabled ? 1U : 0U;
            if (*value_size < sizeof(result)) return -EINVAL;
            std::memcpy(value, &result, sizeof(result));
            *value_size = sizeof(result);
            return 0;
        }
        case kParamPreampDb:
            if (*value_size < sizeof(float)) return -EINVAL;
            std::memcpy(value, &parameters.preamp_db, sizeof(float));
            *value_size = sizeof(float);
            return 0;
        case kParamBalance:
            if (*value_size < sizeof(float)) return -EINVAL;
            std::memcpy(value, &parameters.balance, sizeof(float));
            *value_size = sizeof(float);
            return 0;
        case kParamLimiterEnabled: {
            const std::uint32_t result = parameters.limiter_enabled ? 1U : 0U;
            if (*value_size < sizeof(result)) return -EINVAL;
            std::memcpy(value, &result, sizeof(result));
            *value_size = sizeof(result);
            return 0;
        }
        case kParamLimiterThreshold:
            if (*value_size < sizeof(float)) return -EINVAL;
            std::memcpy(value, &parameters.limiter_threshold, sizeof(float));
            *value_size = sizeof(float);
            return 0;
        default:
            if (id < kParamBandBase ||
                id >= kParamBandBase + pcm_dsp::kEqBandCount) return -EINVAL;
            {
                const auto& band = parameters.bands[id - kParamBandBase];
                BandParameter result{
                    band.frequency_hz, band.gain_db, band.q,
                    band.enabled ? 1U : 0U,
                    static_cast<std::uint32_t>(band.type), band.shelf_slope
                };
                if (*value_size < sizeof(result)) return -EINVAL;
                std::memcpy(value, &result, sizeof(result));
                *value_size = sizeof(result);
                return 0;
            }
    }
}

std::int32_t command_effect(effect_handle_t self,
                            std::uint32_t command_code,
                            std::uint32_t command_size,
                            void* command_data,
                            std::uint32_t* reply_size,
                            void* reply_data) {
    auto* context = context_from_handle(self);
    if (context == nullptr) return -EINVAL;

    std::int32_t status = 0;
    switch (command_code) {
        case EFFECT_CMD_INIT:
            context->control_parameters = pcm_dsp::Parameters{};
            context->has_deferred_parameters = false;
            if (!publish_parameters(context, context->control_parameters)) {
                status = -ENOSPC;
            }
            context->framework_enabled.store(false, std::memory_order_release);
            context->reset_requested.store(true, std::memory_order_release);
            break;
        case EFFECT_CMD_SET_CONFIG:
            if (command_data == nullptr ||
                command_size != sizeof(effect_config_t)) {
                status = -EINVAL;
            } else {
                status = configure_effect(
                    context, *static_cast<const effect_config_t*>(command_data))
                    ? 0 : -EINVAL;
            }
            break;
        case EFFECT_CMD_GET_CONFIG:
            if (reply_size == nullptr || reply_data == nullptr ||
                *reply_size < sizeof(effect_config_t)) {
                status = -EINVAL;
            } else {
                std::memcpy(reply_data, &context->config, sizeof(effect_config_t));
                *reply_size = sizeof(effect_config_t);
                return 0;
            }
            break;
        case EFFECT_CMD_RESET:
            context->reset_requested.store(true, std::memory_order_release);
            break;
        case EFFECT_CMD_ENABLE:
            context->framework_enabled.store(true, std::memory_order_release);
            context->reset_requested.store(true, std::memory_order_release);
            break;
        case EFFECT_CMD_DISABLE:
            context->framework_enabled.store(false, std::memory_order_release);
            context->reset_requested.store(true, std::memory_order_release);
            break;
        case EFFECT_CMD_SET_DEVICE:
            if (command_data == nullptr || command_size != sizeof(std::uint32_t)) {
                status = -EINVAL;
            } else {
                std::uint32_t device = 0U;
                std::memcpy(&device, command_data, sizeof(device));
                context->output_device.store(device, std::memory_order_release);
            }
            break;
        case EFFECT_CMD_SET_AUDIO_MODE:
            if (command_data == nullptr || command_size != sizeof(std::uint32_t)) {
                status = -EINVAL;
            } else {
                std::uint32_t mode = 0U;
                std::memcpy(&mode, command_data, sizeof(mode));
                context->audio_mode.store(mode, std::memory_order_release);
            }
            break;
        case EFFECT_CMD_OFFLOAD:
            if (command_data == nullptr ||
                command_size != sizeof(effect_offload_param_t)) {
                status = -EINVAL;
            } else {
                const auto* offload =
                    static_cast<const effect_offload_param_t*>(command_data);
                context->offload.store(offload->isOffload,
                                       std::memory_order_release);
            }
            break;
        case EFFECT_CMD_SET_PARAM:
        case EFFECT_CMD_SET_PARAM_DEFERRED: {
            if (command_data == nullptr ||
                command_size < offsetof(effect_param_t, data) +
                               sizeof(std::uint32_t)) {
                status = -EINVAL;
                break;
            }
            const auto* parameter = static_cast<const effect_param_t*>(command_data);
            if (parameter->psize != sizeof(std::uint32_t)) {
                status = -EINVAL;
                break;
            }
            const std::size_t value_offset =
                (static_cast<std::size_t>(parameter->psize) + 3U) &
                ~std::size_t(3U);
            const std::size_t required = offsetof(effect_param_t, data) +
                                         value_offset + parameter->vsize;
            if (required > command_size) {
                status = -EINVAL;
                break;
            }
            std::uint32_t id = 0U;
            std::memcpy(&id, parameter->data, sizeof(id));
            status = apply_parameter(context, id,
                parameter->data + value_offset, parameter->vsize,
                command_code == EFFECT_CMD_SET_PARAM_DEFERRED);
            break;
        }
        case EFFECT_CMD_SET_PARAM_COMMIT:
            if (context->has_deferred_parameters) {
                if (!publish_parameters(context, context->deferred_parameters)) {
                    status = -ENOSPC;
                } else {
                    context->control_parameters = context->deferred_parameters;
                    context->has_deferred_parameters = false;
                }
            }
            break;
        case EFFECT_CMD_GET_PARAM: {
            if (command_data == nullptr || reply_data == nullptr ||
                reply_size == nullptr ||
                command_size < offsetof(effect_param_t, data) +
                               sizeof(std::uint32_t)) {
                status = -EINVAL;
                break;
            }
            const auto* request = static_cast<const effect_param_t*>(command_data);
            if (request->psize != sizeof(std::uint32_t)) {
                status = -EINVAL;
                break;
            }
            std::uint32_t id = 0U;
            std::memcpy(&id, request->data, sizeof(id));
            std::array<std::uint8_t, sizeof(BandParameter)> value{};
            std::uint32_t value_size = static_cast<std::uint32_t>(value.size());
            status = get_parameter_value(context->control_parameters, id,
                                         value.data(), &value_size);
            const std::size_t aligned_parameter =
                (request->psize + 3U) & ~std::size_t(3U);
            const std::size_t total = offsetof(effect_param_t, data) +
                                      aligned_parameter + value_size;
            if (status == 0 && *reply_size >= total) {
                auto* response = static_cast<effect_param_t*>(reply_data);
                response->status = 0;
                response->psize = sizeof(std::uint32_t);
                response->vsize = value_size;
                std::memcpy(response->data, &id, sizeof(id));
                std::memcpy(response->data + aligned_parameter,
                            value.data(), value_size);
                *reply_size = static_cast<std::uint32_t>(total);
                return 0;
            }
            if (status == 0) status = -EINVAL;
            break;
        }
        case EFFECT_CMD_SET_CONFIG_REVERSE:
        case EFFECT_CMD_GET_CONFIG_REVERSE:
        case EFFECT_CMD_SET_INPUT_DEVICE:
        case EFFECT_CMD_SET_VOLUME:
        case EFFECT_CMD_SET_AUDIO_SOURCE:
            status = -ENOSYS;
            break;
        default:
            return -EINVAL;
    }

    if (command_code == EFFECT_CMD_SET_PARAM_DEFERRED) {
        if (reply_size != nullptr) *reply_size = 0U;
    } else if (command_code == EFFECT_CMD_SET_DEVICE ||
               command_code == EFFECT_CMD_SET_AUDIO_MODE ||
               command_code == EFFECT_CMD_RESET) {
        if (reply_size != nullptr) *reply_size = 0U;
    } else {
        write_status(reply_size, reply_data, status);
    }
    return 0;
}

std::int32_t get_instance_descriptor(effect_handle_t self,
                                     effect_descriptor_t* descriptor);

effect_interface_s kEffectInterface = {
    process_effect,
    command_effect,
    get_instance_descriptor,
    nullptr
};

const effect_descriptor_t kDescriptor = {
    kEffectTypeEqualizer,
    kEffectUuid,
    EFFECT_CONTROL_API_VERSION,
    EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_LAST |
        EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_AUDIO_MODE_IND |
        EFFECT_FLAG_INPUT_DIRECT | EFFECT_FLAG_OUTPUT_DIRECT,
    120U,
    32U,
    "MT8163 Stereo DSP",
    "14march93 / mt8163_audio_engine"
};

std::int32_t get_instance_descriptor(effect_handle_t self,
                                     effect_descriptor_t* descriptor) {
    if (context_from_handle(self) == nullptr || descriptor == nullptr) {
        return -EINVAL;
    }
    *descriptor = kDescriptor;
    return 0;
}

std::int32_t create_effect(const effect_uuid_t* uuid,
                           std::int32_t /*session_id*/,
                           std::int32_t /*io_id*/,
                           effect_handle_t* handle) {
    if (uuid == nullptr || handle == nullptr) return -EINVAL;
    if (!uuid_equal(*uuid, kEffectUuid)) return -ENOENT;
    auto* context = new (std::nothrow) EffectContext(&kEffectInterface);
    if (context == nullptr) return -ENOMEM;
    *handle = reinterpret_cast<effect_handle_t>(&context->interface);
    return 0;
}

std::int32_t release_effect(effect_handle_t handle) {
    auto* context = context_from_handle(handle);
    if (context == nullptr) return -EINVAL;
    delete context;
    return 0;
}

std::int32_t get_descriptor(const effect_uuid_t* uuid,
                            effect_descriptor_t* descriptor) {
    if (uuid == nullptr || descriptor == nullptr) return -EINVAL;
    if (!uuid_equal(*uuid, kEffectUuid)) return -ENOENT;
    *descriptor = kDescriptor;
    return 0;
}

} // namespace

extern "C" __attribute__((visibility("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    AUDIO_EFFECT_LIBRARY_TAG,
    EFFECT_LIBRARY_API_VERSION,
    "MT8163 Stereo DSP Effect Library",
    "14march93 / mt8163_audio_engine",
    create_effect,
    release_effect,
    get_descriptor
};
