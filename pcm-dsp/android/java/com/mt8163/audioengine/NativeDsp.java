package com.mt8163.audioengine;

/**
 * Java test bridge for the native four-channel Float32 DSP.
 *
 * This array-based JNI API is intended for bring-up and offline validation.
 * Do not call it once per audio callback: JNI array access may copy or pin
 * memory. A future AudioFlinger/HAL adapter must call the C++ engine directly.
 *
 * Buffer layout: interleaved FL, FR, RL, RR normalized Float32 samples.
 */
public final class NativeDsp implements AutoCloseable {
    static {
        System.loadLibrary("mt8163_audio_jni");
    }

    private long nativeHandle;

    public NativeDsp(int sampleRateHz) {
        nativeHandle = nativeCreate(sampleRateHz);
        if (nativeHandle == 0L) {
            throw new IllegalArgumentException("Unsupported sample rate or native DSP initialization failed");
        }
    }

    public synchronized boolean setEnabled(boolean enabled) {
        return nativeHandle != 0L && nativeSetEnabled(nativeHandle, enabled);
    }

    /** Channel order: 0=FL, 1=FR, 2=RL, 3=RR. Gain range: -24..+12 dB. */
    public synchronized boolean setChannelGainDb(int channel, float gainDb) {
        return nativeHandle != 0L && nativeSetChannelGainDb(nativeHandle, channel, gainDb);
    }

    /**
     * Processes frames * 4 interleaved samples.
     * Input and output must be different arrays.
     */
    public synchronized boolean process(float[] input, float[] output, int frames) {
        return nativeHandle != 0L && input != null && output != null
                && nativeProcess(nativeHandle, input, output, frames);
    }

    @Override
    public synchronized void close() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle);
            nativeHandle = 0L;
        }
    }

    private static native long nativeCreate(int sampleRateHz);
    private static native void nativeDestroy(long handle);
    private static native boolean nativeSetEnabled(long handle, boolean enabled);
    private static native boolean nativeSetChannelGainDb(long handle, int channel, float gainDb);
    private static native boolean nativeProcess(long handle, float[] input, float[] output, int frames);
}
