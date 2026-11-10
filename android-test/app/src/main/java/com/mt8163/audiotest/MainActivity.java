package com.mt8163.audiotest;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.mt8163.audioengine.NativeDsp;

import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int CHANNELS = 4;
    private static final int FRAMES = 512;
    private static final String[] CHANNEL_NAMES = {"Front Left", "Front Right", "Rear Left", "Rear Right"};
    private final TextView[] gainLabels = new TextView[CHANNELS];
    private final SeekBar[] gainBars = new SeekBar[CHANNELS];
    private NativeDsp dsp;
    private TextView status;
    private boolean dspEnabled = true;
    private volatile boolean profilePumpRunning;
    private Thread profilePump;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setBackgroundColor(Color.rgb(15, 17, 22));
        buildUi();
        try {
            dsp = new NativeDsp(48000);
            status.setText("DSP ready · 48 kHz · 4 channels · Float32");
            startProfilePump();
        } catch (Throwable error) {
            status.setText("DSP initialization failed: " + error.getMessage());
        }
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(24), dp(20), dp(20));

        TextView title = text("MT8163 AUDIO ENGINE", 22, Color.WHITE);
        root.addView(title);
        TextView subtitle = text("Native four-channel DSP test bench", 13, Color.LTGRAY);
        root.addView(subtitle);

        status = text("Initializing native DSP…", 13, Color.CYAN);
        status.setPadding(0, dp(18), 0, dp(12));
        root.addView(status);

        for (int i = 0; i < CHANNELS; i++) {
            final int channel = i;
            gainLabels[i] = text(CHANNEL_NAMES[i] + " · 0.0 dB", 15, Color.WHITE);
            root.addView(gainLabels[i]);
            gainBars[i] = new SeekBar(this);
            gainBars[i].setMax(360);
            gainBars[i].setProgress(240);
            gainBars[i].setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    float db = (value - 240) / 10.0f;
                    gainLabels[channel].setText(CHANNEL_NAMES[channel] + " · " +
                            String.format(Locale.US, "%.1f dB", db));
                }
                @Override public void onStartTrackingTouch(SeekBar bar) {}
                @Override public void onStopTrackingTouch(SeekBar bar) {
                    if (dsp == null) return;
                    float db = (bar.getProgress() - 240) / 10.0f;
                    if (dsp.setChannelGainDb(channel, db)) {
                        status.setText("Profile applied · 50 ms crossfade");
                    } else {
                        status.setText("DSP busy · wait for the current profile transition");
                    }
                }
            });
            root.addView(gainBars[i]);
        }

        Button toggle = button("DSP: ON");
        toggle.setOnClickListener(v -> {
            dspEnabled = !dspEnabled;
            if (dsp != null && dsp.setEnabled(dspEnabled)) {
                toggle.setText(dspEnabled ? "DSP: ON" : "DSP: OFF");
                status.setText("Profile applied · 50 ms crossfade");
            } else {
                dspEnabled = !dspEnabled;
                Toast.makeText(this, "DSP busy or unavailable", Toast.LENGTH_SHORT).show();
            }
        });
        root.addView(toggle);

        Button run = button("RUN PCM TEST · 512 FRAMES");
        run.setOnClickListener(v -> runPcmTest());
        root.addView(run);

        TextView note = text("Test signal is generated locally. No microphone, speaker output, Bluetooth or system audio is accessed.", 12, Color.GRAY);
        note.setPadding(0, dp(18), 0, 0);
        root.addView(note);
        setContentView(root);
    }

    // Keeps native profile crossfades advancing while the test UI is idle.
    // This is a silent offline pump, not an Android system-audio connection.
    private void startProfilePump() {
        profilePumpRunning = true;
        profilePump = new Thread(() -> {
            float[] input = new float[FRAMES * CHANNELS];
            float[] output = new float[input.length];
            while (profilePumpRunning) {
                NativeDsp current = dsp;
                if (current == null) break;
                current.process(input, output, FRAMES);
                try {
                    Thread.sleep(10L);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }, "DSP-Profile-Pump");
        profilePump.start();
    }

    private void runPcmTest() {
        if (dsp == null) {
            status.setText("Native DSP is not available");
            return;
        }
        float[] input = new float[FRAMES * CHANNELS];
        float[] output = new float[input.length];
        for (int frame = 0; frame < FRAMES; frame++) {
            float sample = (float) (0.2 * Math.sin(2.0 * Math.PI * 1000.0 * frame / 48000.0));
            for (int ch = 0; ch < CHANNELS; ch++) input[frame * CHANNELS + ch] = sample;
        }

        long start = System.nanoTime();
        boolean ok = dsp.process(input, output, FRAMES);
        long elapsedNs = System.nanoTime() - start;
        if (!ok) {
            status.setText("PCM processing failed");
            return;
        }

        double sumSquares = 0.0;
        for (float value : output) sumSquares += value * value;
        double rms = Math.sqrt(sumSquares / output.length);
        double realtimePercent = (elapsedNs / 1_000_000.0) / (FRAMES * 1000.0 / 48000.0) * 100.0;
        status.setText(String.format(Locale.US,
                "PASS · %.2f µs · %.2f%% of block time · RMS %.5f",
                elapsedNs / 1000.0, realtimePercent, rms));
    }

    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setPadding(0, dp(5), 0, dp(5));
        return view;
    }

    private Button button(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setGravity(Gravity.CENTER);
        return button;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    @Override
    protected void onDestroy() {
        profilePumpRunning = false;
        if (profilePump != null) {
            profilePump.interrupt();
            try {
                profilePump.join(250L);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
            }
            profilePump = null;
        }
        if (dsp != null) {
            dsp.close();
            dsp = null;
        }
        super.onDestroy();
    }
}
