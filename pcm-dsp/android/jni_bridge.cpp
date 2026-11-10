#include "pcm_dsp/quad_engine.hpp"

#include <jni.h>

#include <cstdint>
#include <memory>

namespace {

struct EngineHandle {
    pcm_dsp::QuadEngine engine;
    pcm_dsp::QuadParameters parameters{};
};

EngineHandle* from_handle(jlong value) {
    return reinterpret_cast<EngineHandle*>(static_cast<std::uintptr_t>(value));
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeCreate(JNIEnv*, jclass, jint sample_rate) {
    if (sample_rate <= 0) {
        return 0;
    }

    std::unique_ptr<EngineHandle> handle(new EngineHandle());
    if (!handle->engine.configure(static_cast<std::uint32_t>(sample_rate))) {
        return 0;
    }
    handle->parameters.enabled = true;
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
    if (handle == nullptr) {
        return JNI_FALSE;
    }
    handle->parameters.enabled = enabled == JNI_TRUE;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeSetChannelGainDb(
        JNIEnv*, jclass, jlong native_handle, jint channel, jfloat gain_db) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || channel < 0 ||
        channel >= static_cast<jint>(pcm_dsp::kOutputChannelCount) ||
        gain_db < -24.0F || gain_db > 12.0F) {
        return JNI_FALSE;
    }
    handle->parameters.channels[static_cast<std::size_t>(channel)].gain_db = gain_db;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_mt8163_audioengine_NativeDsp_nativeProcess(
        JNIEnv* env, jclass, jlong native_handle,
        jfloatArray input_array, jfloatArray output_array, jint frames) {
    EngineHandle* handle = from_handle(native_handle);
    if (handle == nullptr || input_array == nullptr || output_array == nullptr ||
        input_array == output_array || frames <= 0) {
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
    if (input == nullptr) {
        return JNI_FALSE;
    }
    jfloat* output = env->GetFloatArrayElements(output_array, &output_copied);
    if (output == nullptr) {
        env->ReleaseFloatArrayElements(input_array, input, JNI_ABORT);
        return JNI_FALSE;
    }

    const bool ok = handle->engine.process(
        input, output, static_cast<std::size_t>(frames), handle->parameters);

    env->ReleaseFloatArrayElements(output_array, output, 0);
    env->ReleaseFloatArrayElements(input_array, input, JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}
