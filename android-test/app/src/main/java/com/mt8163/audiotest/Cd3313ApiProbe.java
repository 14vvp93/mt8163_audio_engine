package com.mt8163.audiotest;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * Read-only probe for the OEM CD3313 AudioEffect API.
 *
 * This class only resolves the API and obtains its Binder-backed singleton.
 * It deliberately does not invoke any audio-control method.
 */
public final class Cd3313ApiProbe {
    private Cd3313ApiProbe() {}

    public static String run() {
        try {
            Class<?> apiClass = Class.forName("android.audio.AudioEffect");
            Method getInstance = apiClass.getMethod("getInstance");
            Object instance = getInstance.invoke(null);
            if (instance == null) {
                return "API class found, but getInstance() returned null. No control command was sent.";
            }

            StringBuilder available = new StringBuilder();
            appendMethod(available, apiClass, "setBalance",
                    int.class, int.class, int.class, int.class);
            appendMethod(available, apiClass, "setInputGain", int.class, int.class);
            appendMethod(available, apiClass, "setBassBoost", int.class, int.class);
            appendMethod(available, apiClass, "setLoud", int.class);
            appendMethod(available, apiClass, "setTreble", int.class, int.class);
            appendMethod(available, apiClass, "setSubWoofer", int.class, int.class);

            return "READ-ONLY PASS: android.audio.AudioEffect loaded; "
                    + "getInstance() succeeded and returned a sourceinfo Binder reference.\n"
                    + "Public control methods: " + available + "\n"
                    + "No CD3313 command was sent. Native /dev/audiomux operation is not tested.";
        } catch (InvocationTargetException error) {
            Throwable cause = error.getCause() == null ? error : error.getCause();
            return "API class found, but getInstance() failed: "
                    + cause.getClass().getSimpleName() + ": " + cause.getMessage()
                    + ". No control command was sent.";
        } catch (ClassNotFoundException error) {
            return "OEM API unavailable: android.audio.AudioEffect class was not found. "
                    + "No control command was sent.";
        } catch (Throwable error) {
            return "READ-ONLY probe failed: " + error.getClass().getSimpleName()
                    + ": " + error.getMessage() + ". No control command was sent.";
        }
    }

    private static void appendMethod(
            StringBuilder output, Class<?> apiClass, String name, Class<?>... parameterTypes) {
        if (output.length() > 0) output.append(", ");
        try {
            apiClass.getMethod(name, parameterTypes);
            output.append(name);
        } catch (NoSuchMethodException missing) {
            output.append(name).append("(missing)");
        }
    }
}
