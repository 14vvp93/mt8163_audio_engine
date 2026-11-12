package com.mt8163.audiotest;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.audiofx.AudioEffect;
import android.os.Build;

import java.util.Arrays;

/**
 * Read-only Android audio preflight.
 *
 * Enumerates connected output devices and registered framework effects.
 * It does not create an AudioTrack, attach an effect, change routing, or
 * issue any vendor/CD3313 control command.
 */
public final class AudioRouteProbe {
    private static final String TARGET_EFFECT_UUID =
            "8e5f3c12-7b4d-4a91-9c62-1f08a7d35e20";

    private AudioRouteProbe() {}

    public static String run(Context context) {
        StringBuilder out = new StringBuilder(2048);
        out.append("ANDROID AUDIO PREFLIGHT (READ-ONLY)\n");
        out.append("Model: ").append(Build.MODEL).append('\n');
        out.append("Device: ").append(Build.DEVICE).append('\n');
        out.append("Android: ").append(Build.VERSION.RELEASE)
                .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        out.append("Build: ").append(Build.FINGERPRINT).append('\n');
        out.append("ABIs: ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append('\n');

        AudioManager manager =
                (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (manager == null) {
            out.append("\nAudioManager unavailable.\n");
            return out.toString();
        }

        out.append("\nOUTPUT CONFIGURATION\n");
        appendProperty(out, manager, AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE,
                "Default output sample rate");
        appendProperty(out, manager, AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER,
                "Default output frames per buffer");

        out.append("\nENUMERATED OUTPUT DEVICES\n");
        try {
            AudioDeviceInfo[] devices =
                    manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
            if (devices == null || devices.length == 0) {
                out.append("No output devices returned by AudioManager.\n");
            } else {
                for (AudioDeviceInfo device : devices) {
                    out.append("- id=").append(device.getId())
                            .append(", type=").append(device.getType())
                            .append(", product=").append(device.getProductName())
                            .append("\n  sampleRates=")
                            .append(Arrays.toString(device.getSampleRates()))
                            .append("\n  channelCounts=")
                            .append(Arrays.toString(device.getChannelCounts()))
                            .append("\n  encodings=")
                            .append(Arrays.toString(device.getEncodings()))
                            .append('\n');
                }
            }
        } catch (Throwable error) {
            out.append("Device enumeration failed: ")
                    .append(error.getClass().getSimpleName()).append(": ")
                    .append(error.getMessage()).append('\n');
        }

        out.append("\nREGISTERED AUDIO EFFECTS\n");
        boolean targetFound = false;
        try {
            AudioEffect.Descriptor[] descriptors = AudioEffect.queryEffects();
            if (descriptors == null || descriptors.length == 0) {
                out.append("No effect descriptors returned by AudioEffect.queryEffects().\n");
            } else {
                for (AudioEffect.Descriptor descriptor : descriptors) {
                    String uuid = String.valueOf(descriptor.uuid);
                    boolean target = TARGET_EFFECT_UUID.equalsIgnoreCase(uuid);
                    if (target) targetFound = true;
                    out.append(target ? "* TARGET FOUND: " : "- ")
                            .append(descriptor.name)
                            .append(" | uuid=").append(uuid)
                            .append(" | type=").append(descriptor.type)
                            .append(" | mode=").append(descriptor.connectMode)
                            .append(" | vendor=").append(descriptor.implementor)
                            .append('\n');
                }
            }
        } catch (Throwable error) {
            out.append("Effect enumeration failed: ")
                    .append(error.getClass().getSimpleName()).append(": ")
                    .append(error.getMessage()).append('\n');
        }

        out.append("\nMT8163 STEREO EFFECT: ")
                .append(targetFound ? "REGISTERED IN FRAMEWORK" : "NOT FOUND")
                .append('\n');
        out.append("This probe lists available devices/effects; it does not prove ")
                .append("which device or stream is currently active, nor that a ")
                .append("Music session is attached to the effect.\n");
        out.append("No audio was played, no route was changed, and no control ")
                .append("command was sent.\n");
        return out.toString();
    }

    private static void appendProperty(StringBuilder out, AudioManager manager,
                                       String key, String label) {
        out.append(label).append(": ");
        String value = manager.getProperty(key);
        out.append(value == null ? "unavailable" : value).append('\n');
    }
}
