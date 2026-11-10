package com.mt8163.audiotest;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
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
    private SeekBar balanceBar;
    private SeekBar faderBar;
    private TextView balanceLabel;
    private TextView faderLabel;
    private final float[][] eqGainsDb = new float[CHANNELS][32];
    private final TextView[] eqBandLabels = new TextView[32];
    private final SeekBar[] eqBandBars = new SeekBar[32];
    private int selectedEqChannel = 0;
    private TextView eqChannelTitle;
    private NativeDsp dsp;
    private TextView status;
    private TextView bassStatus;
    private boolean bassManagementEnabled = false;
    private float crossoverHz = 100.0f;
    private int crossoverOrder = 4;
    private float subGainDb = 0.0f;
    private boolean subPhaseInverted = false;
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

        balanceLabel = text("Balance · Center", 15, Color.WHITE);
        root.addView(balanceLabel);
        balanceBar = new SeekBar(this);
        balanceBar.setMax(200);
        balanceBar.setProgress(100);
        balanceBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                balanceLabel.setText("Balance · " + (value == 100 ? "Center" :
                        (value < 100 ? String.format(Locale.US, "%.0f%% Left", 100-value) :
                                String.format(Locale.US, "%.0f%% Right", value-100))));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyBalanceFader(); }
        });
        root.addView(balanceBar);

        faderLabel = text("Fader · Center", 15, Color.WHITE);
        root.addView(faderLabel);
        faderBar = new SeekBar(this);
        faderBar.setMax(200);
        faderBar.setProgress(100);
        faderBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                faderLabel.setText("Fader · " + (value == 100 ? "Center" :
                        (value < 100 ? String.format(Locale.US, "%.0f%% Front", 100-value) :
                                String.format(Locale.US, "%.0f%% Rear", value-100))));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyBalanceFader(); }
        });
        root.addView(faderBar);

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

        TextView eqTitle = text("32-BAND PARAMETRIC EQ", 18, Color.WHITE);
        eqTitle.setPadding(0, dp(22), 0, dp(8));
        root.addView(eqTitle);
        eqChannelTitle = text("EQ channel · Front Left", 14, Color.CYAN);
        root.addView(eqChannelTitle);
        LinearLayout channelPicker = new LinearLayout(this);
        channelPicker.setOrientation(LinearLayout.HORIZONTAL);
        String[] shortNames = {"FL", "FR", "RL", "RR"};
        for (int ch = 0; ch < CHANNELS; ch++) {
            final int channel = ch;
            Button pick = button(shortNames[ch]);
            pick.setOnClickListener(v -> selectEqChannel(channel));
            channelPicker.addView(pick, new LinearLayout.LayoutParams(0, dp(48), 1.0f));
        }
        root.addView(channelPicker);
        Button resetEq = button("RESET SELECTED EQ");
        resetEq.setOnClickListener(v -> resetSelectedEq());
        root.addView(resetEq);

        for (int band = 0; band < 32; band++) {
            final int bandIndex = band;
            double position = band / 31.0;
            int frequency = (int) Math.round(20.0 * Math.pow(1000.0, position));
            String frequencyLabel = frequency >= 1000
                    ? String.format(Locale.US, "%.2f kHz", frequency / 1000.0)
                    : frequency + " Hz";
            eqBandLabels[band] = text(frequencyLabel + " · 0.0 dB", 13, Color.WHITE);
            root.addView(eqBandLabels[band]);
            eqBandBars[band] = new SeekBar(this);
            eqBandBars[band].setMax(48);
            eqBandBars[band].setProgress(24);
            eqBandBars[band].setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    float db = (value - 24) * 0.5f;
                    eqBandLabels[bandIndex].setText(frequencyLabel + " · " +
                            String.format(Locale.US, "%.1f dB", db));
                }
                @Override public void onStartTrackingTouch(SeekBar bar) {}
                @Override public void onStopTrackingTouch(SeekBar bar) {
                    eqGainsDb[selectedEqChannel][bandIndex] =
                            (bar.getProgress() - 24) * 0.5f;
                    applySelectedEq();
                }
            });
            root.addView(eqBandBars[band]);
        }

        TextView bassTitle = text("SUBWOOFER / BASS MANAGEMENT", 18, Color.WHITE);
        bassTitle.setPadding(0, dp(22), 0, dp(8));
        root.addView(bassTitle);
        bassStatus = text("Bass management · OFF", 14, Color.CYAN);
        root.addView(bassStatus);
        Button bassToggle = button("BASS MANAGEMENT: OFF");
        bassToggle.setOnClickListener(v -> {
            bassManagementEnabled = !bassManagementEnabled;
            applyBassManagement();
            bassToggle.setText(bassManagementEnabled
                    ? "BASS MANAGEMENT: ON" : "BASS MANAGEMENT: OFF");
        });
        root.addView(bassToggle);

        TextView crossoverLabel = text("Crossover · 80 Hz", 14, Color.WHITE);
        root.addView(crossoverLabel);
        SeekBar crossoverBar = new SeekBar(this);
        crossoverBar.setMax(360);
        crossoverBar.setProgress(60);
        crossoverBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                crossoverHz = 40.0f + value;
                crossoverLabel.setText(String.format(Locale.US, "Crossover · %.0f Hz", crossoverHz));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyBassManagement(); }
        });
        root.addView(crossoverBar);

        TextView orderLabel = text("Linkwitz–Riley · 24 dB/oct", 14, Color.WHITE);
        root.addView(orderLabel);
        LinearLayout orderPicker = new LinearLayout(this);
        orderPicker.setOrientation(LinearLayout.HORIZONTAL);
        int[] orders = {2, 4, 6, 8};
        for (int order : orders) {
            Button orderButton = button((order * 6) + " dB");
            orderButton.setOnClickListener(v -> {
                crossoverOrder = order;
                orderLabel.setText("Linkwitz–Riley · " + (order * 6) + " dB/oct");
                applyBassManagement();
            });
            orderPicker.addView(orderButton, new LinearLayout.LayoutParams(0, dp(48), 1.0f));
        }
        root.addView(orderPicker);

        TextView subGainLabel = text("Subwoofer level · 0.0 dB", 14, Color.WHITE);
        root.addView(subGainLabel);
        SeekBar subGainBar = new SeekBar(this);
        subGainBar.setMax(360);
        subGainBar.setProgress(240);
        subGainBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                subGainDb = (value - 240) / 10.0f;
                subGainLabel.setText(String.format(Locale.US, "Subwoofer level · %.1f dB", subGainDb));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyBassManagement(); }
        });
        root.addView(subGainBar);
        Button phaseButton = button("SUBWOOFER PHASE: 0°");
        phaseButton.setOnClickListener(v -> {
            subPhaseInverted = !subPhaseInverted;
            phaseButton.setText(subPhaseInverted
                    ? "SUBWOOFER PHASE: 180°" : "SUBWOOFER PHASE: 0°");
            applyBassManagement();
        });
        root.addView(phaseButton);

        Button run = button("RUN PCM TEST · 512 FRAMES");
        run.setOnClickListener(v -> runPcmTest());
        root.addView(run);

        TextView note = text("Test signal is generated locally. No microphone, speaker output, Bluetooth or system audio is accessed.", 12, Color.GRAY);
        note.setPadding(0, dp(18), 0, 0);
        root.addView(note);
        setContentView(new ScrollView(this) {{ setFillViewport(true); addView(root); }});
    }

    private void applyBassManagement() {
        if (dsp == null) return;
        if (dsp.configureBassManagement(bassManagementEnabled, crossoverHz,
                crossoverOrder, subGainDb, subPhaseInverted)) {
            bassStatus.setText(String.format(Locale.US,
                    "Bass management · %s · %.0f Hz · %d dB/oct · Sub %.1f dB",
                    bassManagementEnabled ? "ON" : "OFF", crossoverHz,
                    crossoverOrder * 6, subGainDb));
            status.setText("Bass management profile applied · 50 ms crossfade");
        } else {
            status.setText("DSP busy · wait for the current profile transition");
        }
    }

    private void selectEqChannel(int channel) {
        selectedEqChannel = channel;
        eqChannelTitle.setText("EQ channel · " + CHANNEL_NAMES[channel]);
        for (int band = 0; band < 32; band++) {
            int progress = Math.round(eqGainsDb[channel][band] * 2.0f + 24.0f);
            eqBandBars[band].setProgress(Math.max(0, Math.min(48, progress)));
        }
        status.setText("Editing " + CHANNEL_NAMES[channel] + " EQ");
    }

    private void applySelectedEq() {
        if (dsp == null) return;
        float[] gains = eqGainsDb[selectedEqChannel].clone();
        if (dsp.applyChannelEq(selectedEqChannel, gains)) {
            status.setText(CHANNEL_NAMES[selectedEqChannel] +
                    " EQ applied · 32 bands · 50 ms crossfade");
        } else {
            status.setText("DSP busy · wait for the current profile transition");
        }
    }

    private void resetSelectedEq() {
        java.util.Arrays.fill(eqGainsDb[selectedEqChannel], 0.0f);
        for (SeekBar bar : eqBandBars) bar.setProgress(24);
        applySelectedEq();
    }

    private void applyBalanceFader() {
        if (dsp == null || balanceBar == null || faderBar == null) return;
        float balance = (balanceBar.getProgress() - 100) / 100.0f;
        float fader = (faderBar.getProgress() - 100) / 100.0f;
        if (dsp.applyBalanceFader(balance, fader)) {
            status.setText("Balance / Fader applied · 50 ms crossfade");
        } else {
            status.setText("DSP busy · wait for the current profile transition");
        }
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
