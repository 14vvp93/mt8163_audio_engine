package com.mt8163.audiotest;

import java.lang.reflect.Field;
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

            String binderStatus = "Binder reference obtained";
            try {
                Field serviceField = apiClass.getDeclaredField("mService");
                serviceField.setAccessible(true);
                Object service = serviceField.get(instance);
                if (service == null) {
                    binderStatus = "Binder reference is null";
                } else {
                    Method asBinder = service.getClass().getMethod("asBinder");
                    Object binder = asBinder.invoke(service);
                    Class<?> binderType = Class.forName("android.os.IBinder");
                    Method pingBinder = binderType.getMethod("pingBinder");
                    boolean alive = Boolean.TRUE.equals(pingBinder.invoke(binder));
                    binderStatus = alive ? "sourceinfo Binder ping OK" : "sourceinfo Binder ping failed";
                }
            } catch (Throwable pingError) {
                binderStatus = "Binder reference obtained; ping unavailable: "
                        + pingError.getClass().getSimpleName();
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
                    + "getInstance() succeeded. " + binderStatus + ".\n"
                    + "Public control methods: " + available + "\n"
                    + "No CD3313 control command was sent. Native /dev/audiomux operation is not tested.";
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
