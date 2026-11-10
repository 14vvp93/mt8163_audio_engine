package com.mt8163.audioengine;

/**
 * Java test bridge for the native four-channel Float32 DSP.
 *
 * This array-based JNI API is intended for bring-up and offline validation.
 * Do not call it once per audio callback: JNI array access may copy or pin
 * memory. A future AudioFlinger/HAL adapter must call the C++ engine directly.
 *
 * Profile changes are complete validated snapshots and crossfaded by native
 * code (50 ms by default). The audio thread must keep processing blocks while
 * a transition is pending. A second update is rejected until it completes.
 *
 * Buffer layout: interleaved FL, FR, RL, RR normalized Float32 samples.
 */
public final class NativeDsp implements AutoCloseable {
    public static final int QUAD_PROFILE_SCHEMA_VERSION = 1;
    public static final int OUTPUT_CHANNELS = 4;
    public static final int EQ_BANDS_PER_CHANNEL = 32;
    public static final int QUAD_PROFILE_FLOAT_COUNT =
            6 + OUTPUT_CHANNELS * (2 + EQ_BANDS_PER_CHANNEL * 6);

    // Packed profile: [enabled, preamp dB, limiter enabled, threshold,
    // attack ms, release ms], then per channel [gain dB, delay ms], followed
    // by 32 bands [frequency Hz, gain dB, Q, enabled, filter type, shelf slope].
    // Filter type: 0=Peaking, 1=LowShelf, 2=HighShelf, 3=HighPass, 4=LowPass.
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

    /** Applies all four channels, all 128 EQ bands and limiter settings atomically. */
    public synchronized boolean applyQuadProfile(float[] packedProfile) {
        return nativeHandle != 0L && packedProfile != null
                && packedProfile.length == QUAD_PROFILE_FLOAT_COUNT
                && nativeApplyQuadProfile(nativeHandle, packedProfile);
    }

    /** Atomically applies 32 peaking-EQ gains (-18..+18 dB) to one output channel. */
    public synchronized boolean applyChannelEq(int channel, float[] gainsDb) {
        return nativeHandle != 0L && channel >= 0 && channel < OUTPUT_CHANNELS
                && gainsDb != null && gainsDb.length == EQ_BANDS_PER_CHANNEL
                && nativeApplyChannelEq(nativeHandle, channel, gainsDb);
    }

    /** Balance: -1=left, +1=right. Fader: -1=front, +1=rear. */
    public synchronized boolean applyBalanceFader(float balance, float fader) {
        return nativeHandle != 0L && nativeApplyBalanceFader(nativeHandle, balance, fader);
    }

    /** Creates a flat, disabled 32-band PEQ profile for each output channel. */
    public static float[] createDefaultQuadProfile() {
        float[] profile = new float[QUAD_PROFILE_FLOAT_COUNT];
        profile[0] = 1.0f;  // DSP enabled
        profile[1] = 0.0f;  // preamp
        profile[2] = 1.0f;  // limiter enabled
        profile[3] = 0.98f; // limiter threshold
        profile[4] = 5.0f;  // attack
        profile[5] = 80.0f; // release

        int offset = 6;
        for (int channel = 0; channel < OUTPUT_CHANNELS; channel++) {
            profile[offset++] = 0.0f; // channel gain
            profile[offset++] = 0.0f; // time alignment
            for (int band = 0; band < EQ_BANDS_PER_CHANNEL; band++) {
                double position = band / (double) (EQ_BANDS_PER_CHANNEL - 1);
                profile[offset++] = (float) (20.0 * Math.pow(1000.0, position));
                profile[offset++] = 0.0f; // gain
                profile[offset++] = 1.0f; // Q
                profile[offset++] = 0.0f; // disabled
                profile[offset++] = 0.0f; // peaking
                profile[offset++] = 1.0f; // shelf slope
            }
        }
        return profile;
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
    private static native boolean nativeApplyQuadProfile(long handle, float[] packedProfile);
    private static native boolean nativeApplyBalanceFader(long handle, float balance, float fader);\n    private static native boolean nativeApplyChannelEq(long handle, int channel, float[] gainsDb);
    private static native boolean nativeProcess(long handle, float[] input, float[] output, int frames);
}
