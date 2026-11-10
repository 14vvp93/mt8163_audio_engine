#include "pcm_dsp/profiled_quad_engine.hpp"

#include <jni.h>

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

    const bool ok = handle->engine.process(
        input, output, static_cast<std::size_t>(frames));
    env->ReleaseFloatArrayElements(output_array, output, 0);
    env->ReleaseFloatArrayElements(input_array, input, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}
