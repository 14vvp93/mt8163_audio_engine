#include "pcm_dsp/profiled_quad_engine.hpp"

#include <jni.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace {

constexpr std::size_t kGlobalFloatCount = 6U;
constexpr std::size_t kChannelControlFloatCount = 2U;
constexpr std::size_t kBandFloatCount = 6U;
constexpr std::size_t kProfileFloatCount =
    kGlobalFloatCount +
    pcm_dsp::kOutputChannelCount *
        (kChannelControlFloatCount + pcm_dsp::kQuadEqBandCount * kBandFloatCount);

struct EngineHandle {
    pcm_dsp::ProfiledQuadEngine engine;
    pcm_dsp::AudioProfile profile{};
};

EngineHandle* from_handle(jlong value) {
    return reinterpret_cast<EngineHandle*>(static_cast<std::uintptr_t>(value));
}

bool read_boolean(float value, bool& result) {
    if (value == 0.0F) {
        result = false;
        return true;
    }
    if (value == 1.0F) {
        result = true;
        return true;
    }
    return false;
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeCreate(JNIEnv*, jclass, jint sample_rate) {
    if (sample_rate <= 0) return 0;

    std::unique_ptr<EngineHandle> handle(new EngineHandle());
    handle->profile.quad.enabled = true;
    if (!handle->engine.configure(
            static_cast<std::uint32_t>(sample_rate), handle->profile)) {
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(handle.release()));
}

extern "C" JNIEXPORT void JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeDestroy(JNIEnv*, jclass, jlong native_handle) {
    delete from_handle(native_handle);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeSetEnabled(
        JNIEnv*, jclass, jlong native_handle, jboolean enabled) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr) return JNI_FALSE;

    pcm_dsp::AudioProfile candidate = handle->profile;
    candidate.quad.enabled = enabled == JNI_TRUE;
    if (!handle->engine.apply_profile(candidate)) return JNI_FALSE;
    handle->profile = candidate;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeSetChannelGainDb(
        JNIEnv*, jclass, jlong native_handle, jint channel, jfloat gain_db) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || channel < 0 ||
        channel >= static_cast<jint>(pcm_dsp::kOutputChannelCount) ||
        !std::isfinite(gain_db) || gain_db < -24.0F || gain_db > 12.0F) {
        return JNI_FALSE;
    }

    pcm_dsp::AudioProfile candidate = handle->profile;
    candidate.quad.channels[static_cast<std::size_t>(channel)].gain_db = gain_db;
    if (!handle->engine.apply_profile(candidate)) return JNI_FALSE;
    handle->profile = candidate;
    return JNI_TRUE;
}

// Packed quad profile layout is versioned and documented in NativeDsp.java.
// A complete candidate is validated and queued as one crossfaded profile.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeApplyQuadProfile(
        JNIEnv* env, jclass, jlong native_handle, jfloatArray packed_array) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || packed_array == nullptr ||
        env->GetArrayLength(packed_array) !=
            static_cast<jsize>(kProfileFloatCount)) {
        return JNI_FALSE;
    }

    std::array<jfloat, kProfileFloatCount> values{};
    env->GetFloatArrayRegion(packed_array, 0,
        static_cast<jsize>(values.size()), values.data());
    if (env->ExceptionCheck()) return JNI_FALSE;

    pcm_dsp::AudioProfile candidate = handle->profile;
    bool quad_enabled = false;
    bool limiter_enabled = false;
    if (!read_boolean(values[0], quad_enabled) ||
        !std::isfinite(values[1]) ||
        !read_boolean(values[2], limiter_enabled) ||
        !std::isfinite(values[3]) || !std::isfinite(values[4]) ||
        !std::isfinite(values[5])) {
        return JNI_FALSE;
    }
    candidate.quad.enabled = quad_enabled;
    candidate.quad.preamp_db = values[1];
    candidate.quad.limiter_enabled = limiter_enabled;
    candidate.quad.limiter_threshold = values[3];
    candidate.quad.limiter_attack_ms = values[4];
    candidate.quad.limiter_release_ms = values[5];

    std::size_t offset = kGlobalFloatCount;
    for (std::size_t channel = 0;
         channel < pcm_dsp::kOutputChannelCount; ++channel) {
        auto& channel_parameters = candidate.quad.channels[channel];
        channel_parameters.gain_db = values[offset++];
        channel_parameters.delay_ms = values[offset++];

        for (std::size_t band = 0; band < pcm_dsp::kQuadEqBandCount; ++band) {
            auto& eq = channel_parameters.bands[band];
            eq.frequency_hz = values[offset++];
            eq.gain_db = values[offset++];
            eq.q = values[offset++];
            bool band_enabled = false;
            if (!read_boolean(values[offset], band_enabled)) return JNI_FALSE;
            ++offset;

            const float type_value = values[offset++];
            if (!std::isfinite(type_value) || type_value < 0.0F ||
                type_value > 4.0F || std::floor(type_value) != type_value) {
                return JNI_FALSE;
            }
            eq.type = static_cast<pcm_dsp::FilterType>(
                static_cast<std::uint8_t>(type_value));
            eq.enabled = band_enabled;
            eq.shelf_slope = values[offset++];
        }
    }

    if (offset != values.size() ||
        !handle->engine.apply_profile(candidate)) {
        return JNI_FALSE;
    }
    handle->profile = candidate;
    return JNI_TRUE;
}

// Applies one complete 32-band channel EQ as a single profile snapshot.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeApplyChannelEq(
        JNIEnv* env, jclass, jlong native_handle, jint channel,
        jfloatArray gains_array) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || gains_array == nullptr || channel < 0 ||
        channel >= static_cast<jint>(pcm_dsp::kOutputChannelCount) ||
        env->GetArrayLength(gains_array) !=
            static_cast<jsize>(pcm_dsp::kQuadEqBandCount)) {
        return JNI_FALSE;
    }
    std::array<jfloat, pcm_dsp::kQuadEqBandCount> gains{};
    env->GetFloatArrayRegion(gains_array, 0,
        static_cast<jsize>(gains.size()), gains.data());
    if (env->ExceptionCheck()) return JNI_FALSE;

    pcm_dsp::AudioProfile candidate = handle->profile;
    auto& bands = candidate.quad.channels[static_cast<std::size_t>(channel)].bands;
    for (std::size_t band = 0; band < bands.size(); ++band) {
        const float gain = gains[band];
        if (!std::isfinite(gain) || gain < -18.0F || gain > 18.0F) {
            return JNI_FALSE;
        }
        const float position = static_cast<float>(band) /
            static_cast<float>(bands.size() - 1U);
        bands[band].frequency_hz = 20.0F * std::pow(1000.0F, position);
        bands[band].gain_db = gain;
        bands[band].q = 1.0F;
        bands[band].enabled = std::fabs(gain) >= 0.01F;
        bands[band].type = pcm_dsp::FilterType::Peaking;
        bands[band].shelf_slope = 1.0F;
    }
    if (!handle->engine.apply_profile(candidate)) return JNI_FALSE;
    handle->profile = candidate;
    return JNI_TRUE;
}

// Bass management controls: crossover frequency, LR order and sub level.
// Order values are 2, 4, 6 or 8 (12/24/36/48 dB per octave).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeConfigureBassManagement(
        JNIEnv*, jclass, jlong native_handle, jboolean enabled,
        jfloat frequency_hz, jint order, jfloat sub_gain_db,
        jboolean phase_inverted) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || !std::isfinite(frequency_hz) ||
        !std::isfinite(sub_gain_db) || frequency_hz < 20.0F ||
        frequency_hz > 400.0F || sub_gain_db < -24.0F ||
        sub_gain_db > 12.0F || (order != 2 && order != 4 && order != 6 && order != 8)) {
        return JNI_FALSE;
    }
    pcm_dsp::AudioProfile candidate = handle->profile;
    candidate.routing.enabled = true;
    candidate.routing.bass_management_enabled = enabled == JNI_TRUE;
    candidate.routing.crossover_frequency_hz = frequency_hz;
    candidate.routing.crossover_order =
        static_cast<pcm_dsp::LinkwitzRileyOrder>(order);
    candidate.routing.sub_gain_db = sub_gain_db;
    candidate.routing.sub_phase_inverted = phase_inverted == JNI_TRUE;
    if (!handle->engine.apply_profile(candidate)) return JNI_FALSE;
    handle->profile = candidate;
    return JNI_TRUE;
}

// Applies both psychoacoustic enhancement modules as one profile snapshot.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeConfigureEnhancements(
        JNIEnv*, jclass, jlong native_handle,
        jboolean bass_enabled, jfloat bass_amount, jfloat bass_frequency_hz,
        jboolean clarity_enabled, jfloat clarity_amount,
        jfloat clarity_frequency_hz) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || !std::isfinite(bass_amount) ||
        !std::isfinite(bass_frequency_hz) ||
        !std::isfinite(clarity_amount) ||
        !std::isfinite(clarity_frequency_hz) ||
        bass_amount < 0.0F || bass_amount > 1.0F ||
        bass_frequency_hz < 40.0F || bass_frequency_hz > 160.0F ||
        clarity_amount < 0.0F || clarity_amount > 1.0F ||
        clarity_frequency_hz < 1500.0F || clarity_frequency_hz > 8000.0F) {
        return JNI_FALSE;
    }
    pcm_dsp::AudioProfile candidate = handle->profile;
    candidate.enhancements.bass_enabled = bass_enabled == JNI_TRUE;
    candidate.enhancements.bass_amount = bass_amount;
    candidate.enhancements.bass_frequency_hz = bass_frequency_hz;
    candidate.enhancements.clarity_enabled = clarity_enabled == JNI_TRUE;
    candidate.enhancements.clarity_amount = clarity_amount;
    candidate.enhancements.clarity_frequency_hz = clarity_frequency_hz;
    if (!handle->engine.apply_profile(candidate)) return JNI_FALSE;
    handle->profile = candidate;
    return JNI_TRUE;
}

// Balance and fader are represented by a diagonal 4x4 output-routing matrix.
// Center is unity; moving a control attenuates only the opposite side/row pair.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeApplyBalanceFader(
        JNIEnv*, jclass, jlong native_handle, jfloat balance, jfloat fader) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || !std::isfinite(balance) ||
        !std::isfinite(fader) || balance < -1.0F || balance > 1.0F ||
        fader < -1.0F || fader > 1.0F) {
        return JNI_FALSE;
    }
    constexpr float kHalfPi = 1.57079632679489661923F;
    const float left_gain = balance > 0.0F ? std::cos(balance * kHalfPi) : 1.0F;
    const float right_gain = balance < 0.0F ? std::cos(-balance * kHalfPi) : 1.0F;
    const float front_gain = fader > 0.0F ? std::cos(fader * kHalfPi) : 1.0F;
    const float rear_gain = fader < 0.0F ? std::cos(-fader * kHalfPi) : 1.0F;

    pcm_dsp::AudioProfile candidate = handle->profile;
    auto& routing = candidate.routing;
    routing.enabled = true;
    routing.matrix.fill(0.0F);
    routing.matrix[0] = left_gain * front_gain;
    routing.matrix[5] = right_gain * front_gain;
    routing.matrix[10] = left_gain * rear_gain;
    routing.matrix[15] = right_gain * rear_gain;
    if (!handle->engine.apply_profile(candidate)) return JNI_FALSE;
    handle->profile = candidate;
    return JNI_TRUE;
}


extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeApplyUiProfile(
        JNIEnv* env, jclass, jlong native_handle, jfloatArray profile_array) {
    EngineHandle* handle = from_handle(native_handle);
    constexpr std::size_t kUiProfileCount = 233U;
    if (handle == nullptr || profile_array == nullptr ||
        env->GetArrayLength(profile_array) != static_cast<jsize>(kUiProfileCount)) {
        return JNI_FALSE;
    }

    std::array<jfloat, kUiProfileCount> values{};
    env->GetFloatArrayRegion(profile_array, 0,
        static_cast<jsize>(values.size()), values.data());
    if (env->ExceptionCheck()) return JNI_FALSE;

    for (const float value : values) {
        if (!std::isfinite(value)) return JNI_FALSE;
    }
    bool enabled = false, bass_management = false, phase = false;
    bool bass_enabled = false, clarity_enabled = false;
    if (!read_boolean(values[0], enabled) ||
        !read_boolean(values[135], bass_management) ||
        !read_boolean(values[139], phase) ||
        !read_boolean(values[140], bass_enabled) ||
        !read_boolean(values[143], clarity_enabled)) {
        return JNI_FALSE;
    }
    const float balance = values[133];
    const float fader = values[134];
    const float crossover_hz = values[136];
    const float order_value = values[137];
    const float sub_gain_db = values[138];
    if (balance < -1.0F || balance > 1.0F ||
        fader < -1.0F || fader > 1.0F ||
        order_value < 2.0F || order_value > 8.0F ||
        std::floor(order_value) != order_value ||
        (order_value != 2.0F && order_value != 4.0F &&
         order_value != 6.0F && order_value != 8.0F)) {
        return JNI_FALSE;
    }

    pcm_dsp::AudioProfile candidate = handle->profile;
    candidate.quad.enabled = enabled;
    std::size_t offset = 1U;
    for (std::size_t channel = 0; channel < pcm_dsp::kOutputChannelCount; ++channel) {
        auto& parameters = candidate.quad.channels[channel];
        parameters.gain_db = values[offset++];
        for (std::size_t band = 0; band < pcm_dsp::kQuadEqBandCount; ++band) {
            const float gain = values[offset++];
            if (gain < -18.0F || gain > 18.0F) return JNI_FALSE;
            const float position = static_cast<float>(band) /
                static_cast<float>(pcm_dsp::kQuadEqBandCount - 1U);
            auto& eq = parameters.bands[band];
            eq.frequency_hz = 20.0F * std::pow(1000.0F, position);
            eq.gain_db = gain;
            eq.q = 1.0F;
            eq.enabled = std::fabs(gain) >= 0.01F;
            eq.type = pcm_dsp::FilterType::Peaking;
            eq.shelf_slope = 1.0F;
        }
    }

    constexpr float kHalfPi = 1.57079632679489661923F;
    const float left_gain = balance > 0.0F ? std::cos(balance * kHalfPi) : 1.0F;
    const float right_gain = balance < 0.0F ? std::cos(-balance * kHalfPi) : 1.0F;
    const float front_gain = fader > 0.0F ? std::cos(fader * kHalfPi) : 1.0F;
    const float rear_gain = fader < 0.0F ? std::cos(-fader * kHalfPi) : 1.0F;
    auto& routing = candidate.routing;
    routing.enabled = true;
    routing.matrix.fill(0.0F);
    routing.matrix[0] = left_gain * front_gain;
    routing.matrix[5] = right_gain * front_gain;
    routing.matrix[10] = left_gain * rear_gain;
    routing.matrix[15] = right_gain * rear_gain;
    routing.bass_management_enabled = bass_management;
    routing.crossover_frequency_hz = crossover_hz;
    routing.crossover_order =
        static_cast<pcm_dsp::LinkwitzRileyOrder>(static_cast<std::uint8_t>(order_value));
    routing.sub_gain_db = sub_gain_db;
    routing.sub_phase_inverted = phase;

    candidate.enhancements.bass_enabled = bass_enabled;
    candidate.enhancements.bass_amount = values[141];
    candidate.enhancements.bass_frequency_hz = values[142];
    candidate.enhancements.clarity_enabled = clarity_enabled;
    candidate.enhancements.clarity_amount = values[144];
    candidate.enhancements.clarity_frequency_hz = values[145];

    std::size_t dynamics_offset = 146U;
    bool dynamics_enabled = false;
    if (!read_boolean(values[dynamics_offset++], dynamics_enabled)) return JNI_FALSE;
    candidate.dynamics.enabled = dynamics_enabled;
    candidate.dynamics.low_crossover_hz = values[dynamics_offset++];
    candidate.dynamics.high_crossover_hz = values[dynamics_offset++];
    for (std::size_t channel = 0; channel < pcm_dsp::kDynamicsChannelCount; ++channel) {
        for (std::size_t band = 0; band < pcm_dsp::kDynamicsBandCount; ++band) {
            auto& compressor = candidate.dynamics.compressors[channel][band];
            if (!read_boolean(values[dynamics_offset++], compressor.enabled)) {
                return JNI_FALSE;
            }
            compressor.threshold_db = values[dynamics_offset++];
            compressor.ratio = values[dynamics_offset++];
            compressor.attack_ms = values[dynamics_offset++];
            compressor.release_ms = values[dynamics_offset++];
            compressor.knee_db = values[dynamics_offset++];
            compressor.makeup_gain_db = values[dynamics_offset++];
        }
    }

    if (offset != 133U || dynamics_offset != kUiProfileCount ||
        !pcm_dsp::validate_audio_profile(candidate,
            handle->engine.sample_rate()) ||
        !handle->engine.apply_profile(candidate)) {
        return JNI_FALSE;
    }
    handle->profile = candidate;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeProcess(
        JNIEnv* env, jclass, jlong native_handle,
        jfloatArray input_array, jfloatArray output_array, jint frames) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || input_array == nullptr || output_array == nullptr ||
        input_array == output_array || frames <= 0 ||
        frames > std::numeric_limits<jint>::max() /
                     static_cast<jint>(pcm_dsp::kOutputChannelCount)) {
        return JNI_FALSE;
    }

    const jsize required = frames * static_cast<jint>(pcm_dsp::kOutputChannelCount);
    if (env->GetArrayLength(input_array) < required ||
        env->GetArrayLength(output_array) < required) {
        return JNI_FALSE;
    }

    jboolean input_copied = JNI_FALSE;
    jboolean output_copied = JNI_FALSE;
    jfloat* input = env->GetFloatArrayElements(input_array, &input_copied);
    if (input == nullptr) return JNI_FALSE;
    jfloat* output = env->GetFloatArrayElements(output_array, &output_copied);
    if (output == nullptr) {
        env->ReleaseFloatArrayElements(input_array, input, JNI_ABORT);
        return JNI_FALSE;
    }

    // Offline JNI path supplies a dedicated mono subwoofer buffer. Process in
    // bounded chunks so the stack scratch remains fixed-size on ARMv7.
    std::array<float, 1024> sub_scratch{};
    bool ok = true;
    std::size_t offset_frames = 0U;
    while (offset_frames < static_cast<std::size_t>(frames)) {
        const std::size_t chunk = std::min<std::size_t>(
            sub_scratch.size(), static_cast<std::size_t>(frames) - offset_frames);
        ok = handle->engine.process(
            input + offset_frames * pcm_dsp::kOutputChannelCount,
            output + offset_frames * pcm_dsp::kOutputChannelCount,
            chunk, sub_scratch.data());
        if (!ok) break;
        offset_frames += chunk;
    }
    env->ReleaseFloatArrayElements(output_array, output, 0);
    env->ReleaseFloatArrayElements(input_array, input, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}
