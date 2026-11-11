package com.mt8163.audiotest;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import com.mt8163.audioengine.NativeDsp;

import java.util.Locale;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONException;

public final class MainActivity extends Activity {
    private static final int CHANNELS = 4;
    private static final int FRAMES = 512;
    private static final String[] CHANNEL_NAMES = {"Front Left", "Front Right", "Rear Left", "Rear Right"};
    private final TextView[] gainLabels = new TextView[CHANNELS];
    private final SeekBar[] gainBars = new SeekBar[CHANNELS];
    private SeekBar balanceBar;
    private SeekBar crossoverBar;
    private SeekBar subGainBar;
    private SeekBar bassAmountBar;
    private SeekBar bassFrequencyBar;
    private SeekBar clarityAmountBar;
    private SeekBar clarityFrequencyBar;
    private Button dspToggleButton;
    private Button bassToggleButton;
    private Button bassEnhancementToggle;
    private Button clarityToggle;
    private Button phaseButton;
    private TextView crossoverLabel;
    private TextView orderLabel;
    private TextView subGainLabel;
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
    private boolean bassEnhancementEnabled = false;
    private float bassEnhancementAmount = 0.50f;
    private float bassEnhancementFrequencyHz = 90.0f;
    private boolean clarityEnhancementEnabled = false;
    private float clarityEnhancementAmount = 0.30f;
    private float clarityEnhancementFrequencyHz = 2500.0f;
    private boolean dspEnabled = true;
    private boolean dynamicsEnabled = false;
    private float dynamicsLowCrossoverHz = 250.0f;
    private float dynamicsHighCrossoverHz = 4000.0f;
    // Compressor tuple: enabled, threshold, ratio, attack, release, knee, makeup.
    private final float[][][] compressorSettings = new float[CHANNELS][3][7];
    private int selectedDynamicsChannel = 0;
    private int selectedDynamicsBand = 0;
    private Button dynamicsToggleButton;
    private Button dynamicsChannelButton;
    private Button dynamicsBandButton;
    private Button compressorEnabledButton;
    private SeekBar dynamicsLowBar;
    private SeekBar dynamicsHighBar;
    private TextView dynamicsLowLabel;
    private TextView dynamicsHighLabel;
    private final SeekBar[] compressorBars = new SeekBar[6];
    private final TextView[] compressorLabels = new TextView[6];
    private volatile boolean profilePumpRunning;
    private Thread profilePump;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setBackgroundColor(Color.rgb(15, 17, 22));
        initializeDynamicsDefaults();
        buildUi();
        try {
            dsp = new NativeDsp(48000);
            status.setText("DSP ready · 48 kHz · 4 channels · Float32");
            startProfilePump();
            restoreLastSession();
        } catch (Throwable error) {
            status.setText("DSP initialization failed: " + error.getMessage());
        }
    }

    private void initializeDynamicsDefaults() {
        for (int channel = 0; channel < CHANNELS; channel++) {
            for (int band = 0; band < 3; band++) {
                compressorSettings[channel][band][0] = 0.0f;
                compressorSettings[channel][band][1] = -18.0f;
                compressorSettings[channel][band][2] = 2.0f;
                compressorSettings[channel][band][3] = 10.0f;
                compressorSettings[channel][band][4] = 120.0f;
                compressorSettings[channel][band][5] = 6.0f;
                compressorSettings[channel][band][6] = 0.0f;
            }
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

        LinearLayout profileActions = new LinearLayout(this);
        profileActions.setOrientation(LinearLayout.HORIZONTAL);
        Button saveProfile = button("SAVE PROFILE");
        saveProfile.setOnClickListener(v -> promptSaveProfile());
        Button loadProfile = button("LOAD PROFILE");
        loadProfile.setOnClickListener(v -> showSavedProfiles());
        profileActions.addView(saveProfile, new LinearLayout.LayoutParams(0, dp(48), 1.0f));
        profileActions.addView(loadProfile, new LinearLayout.LayoutParams(0, dp(48), 1.0f));
        root.addView(profileActions);

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

        dspToggleButton = button("DSP: ON");
        dspToggleButton.setOnClickListener(v -> {
            dspEnabled = !dspEnabled;
            if (dsp != null && dsp.setEnabled(dspEnabled)) {
                dspToggleButton.setText(dspEnabled ? "DSP: ON" : "DSP: OFF");
                status.setText("Profile applied · 50 ms crossfade");
            } else {
                dspEnabled = !dspEnabled;
                Toast.makeText(this, "DSP busy or unavailable", Toast.LENGTH_SHORT).show();
            }
        });
        root.addView(dspToggleButton);

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
        bassToggleButton = button("BASS MANAGEMENT: OFF");
        bassToggleButton.setOnClickListener(v -> {
            bassManagementEnabled = !bassManagementEnabled;
            applyBassManagement();
            bassToggleButton.setText(bassManagementEnabled
                    ? "BASS MANAGEMENT: ON" : "BASS MANAGEMENT: OFF");
        });
        root.addView(bassToggleButton);

        crossoverLabel = text("Crossover · 100 Hz", 14, Color.WHITE);
        root.addView(crossoverLabel);
        crossoverBar = new SeekBar(this);
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

        orderLabel = text("Linkwitz–Riley · 24 dB/oct", 14, Color.WHITE);
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

        subGainLabel = text("Subwoofer level · 0.0 dB", 14, Color.WHITE);
        root.addView(subGainLabel);
        subGainBar = new SeekBar(this);
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
        phaseButton = button("SUBWOOFER PHASE: 0°");
        phaseButton.setOnClickListener(v -> {
            subPhaseInverted = !subPhaseInverted;
            phaseButton.setText(subPhaseInverted
                    ? "SUBWOOFER PHASE: 180°" : "SUBWOOFER PHASE: 0°");
            applyBassManagement();
        });
        root.addView(phaseButton);

        TextView enhancementTitle = text("PSYCHOACOUSTIC ENHANCEMENT", 18, Color.WHITE);
        enhancementTitle.setPadding(0, dp(22), 0, dp(8));
        root.addView(enhancementTitle);

        bassEnhancementToggle = button("BASS ENHANCEMENT: OFF");
        bassEnhancementToggle.setOnClickListener(v -> {
            bassEnhancementEnabled = !bassEnhancementEnabled;
            bassEnhancementToggle.setText(bassEnhancementEnabled
                    ? "BASS ENHANCEMENT: ON" : "BASS ENHANCEMENT: OFF");
            applyEnhancements();
        });
        root.addView(bassEnhancementToggle);

        TextView bassAmountLabel = text("Bass amount · 50%", 14, Color.WHITE);
        root.addView(bassAmountLabel);
        bassAmountBar = new SeekBar(this);
        bassAmountBar.setMax(100);
        bassAmountBar.setProgress(50);
        bassAmountBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                bassEnhancementAmount = value / 100.0f;
                bassAmountLabel.setText("Bass amount · " + value + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyEnhancements(); }
        });
        root.addView(bassAmountBar);

        TextView bassFrequencyLabel = text("Bass focus · 90 Hz", 14, Color.WHITE);
        root.addView(bassFrequencyLabel);
        bassFrequencyBar = new SeekBar(this);
        bassFrequencyBar.setMax(120);
        bassFrequencyBar.setProgress(50);
        bassFrequencyBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                bassEnhancementFrequencyHz = 40.0f + value;
                bassFrequencyLabel.setText(String.format(Locale.US,
                        "Bass focus · %.0f Hz", bassEnhancementFrequencyHz));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyEnhancements(); }
        });
        root.addView(bassFrequencyBar);

        clarityToggle = button("CLARITY ENHANCEMENT: OFF");
        clarityToggle.setOnClickListener(v -> {
            clarityEnhancementEnabled = !clarityEnhancementEnabled;
            clarityToggle.setText(clarityEnhancementEnabled
                    ? "CLARITY ENHANCEMENT: ON" : "CLARITY ENHANCEMENT: OFF");
            applyEnhancements();
        });
        root.addView(clarityToggle);

        TextView clarityAmountLabel = text("Clarity amount · 30%", 14, Color.WHITE);
        root.addView(clarityAmountLabel);
        clarityAmountBar = new SeekBar(this);
        clarityAmountBar.setMax(100);
        clarityAmountBar.setProgress(30);
        clarityAmountBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                clarityEnhancementAmount = value / 100.0f;
                clarityAmountLabel.setText("Clarity amount · " + value + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyEnhancements(); }
        });
        root.addView(clarityAmountBar);

        TextView clarityFrequencyLabel = text("Clarity focus · 2.5 kHz", 14, Color.WHITE);
        root.addView(clarityFrequencyLabel);
        clarityFrequencyBar = new SeekBar(this);
        clarityFrequencyBar.setMax(550);
        clarityFrequencyBar.setProgress(100);
        clarityFrequencyBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                clarityEnhancementFrequencyHz = 1500.0f + value * 10.0f;
                clarityFrequencyLabel.setText(String.format(Locale.US,
                        "Clarity focus · %.1f kHz", clarityEnhancementFrequencyHz / 1000.0f));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) { applyEnhancements(); }
        });
        root.addView(clarityFrequencyBar);

        TextView dynamicsTitle = text("4-CHANNEL / 3-BAND DYNAMICS", 18, Color.WHITE);
        dynamicsTitle.setPadding(0, dp(22), 0, dp(8));
        root.addView(dynamicsTitle);
        dynamicsToggleButton = button("MULTIBAND DYNAMICS: OFF");
        dynamicsToggleButton.setOnClickListener(v -> {
            dynamicsEnabled = !dynamicsEnabled;
            dynamicsToggleButton.setText(dynamicsEnabled
                    ? "MULTIBAND DYNAMICS: ON" : "MULTIBAND DYNAMICS: OFF");
            applyDynamicsProfile();
        });
        root.addView(dynamicsToggleButton);

        dynamicsLowLabel = text("Low / Mid crossover · 250 Hz", 14, Color.WHITE);
        root.addView(dynamicsLowLabel);
        dynamicsLowBar = new SeekBar(this);
        dynamicsLowBar.setMax(940);
        dynamicsLowBar.setProgress(190);
        dynamicsLowBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                dynamicsLowCrossoverHz = 60.0f + value;
                dynamicsLowLabel.setText(String.format(Locale.US,
                        "Low / Mid crossover · %.0f Hz", dynamicsLowCrossoverHz));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {
                if (dynamicsHighCrossoverHz < dynamicsLowCrossoverHz * 1.5f) {
                    dynamicsHighCrossoverHz = Math.min(8000.0f,
                            (float) Math.ceil(dynamicsLowCrossoverHz * 1.5f / 10.0f) * 10.0f);
                    dynamicsHighBar.setProgress(Math.round((dynamicsHighCrossoverHz - 1000.0f) / 10.0f));
                }
                applyDynamicsProfile();
            }
        });
        root.addView(dynamicsLowBar);

        dynamicsHighLabel = text("Mid / High crossover · 4.0 kHz", 14, Color.WHITE);
        root.addView(dynamicsHighLabel);
        dynamicsHighBar = new SeekBar(this);
        dynamicsHighBar.setMax(700);
        dynamicsHighBar.setProgress(300);
        dynamicsHighBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                dynamicsHighCrossoverHz = 1000.0f + value * 10.0f;
                dynamicsHighLabel.setText(String.format(Locale.US,
                        "Mid / High crossover · %.1f kHz", dynamicsHighCrossoverHz / 1000.0f));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {
                if (dynamicsHighCrossoverHz < dynamicsLowCrossoverHz * 1.5f) {
                    dynamicsHighCrossoverHz = Math.min(8000.0f,
                            (float) Math.ceil(dynamicsLowCrossoverHz * 1.5f / 10.0f) * 10.0f);
                    dynamicsHighBar.setProgress(Math.round((dynamicsHighCrossoverHz - 1000.0f) / 10.0f));
                }
                applyDynamicsProfile();
            }
        });
        root.addView(dynamicsHighBar);

        LinearLayout dynamicsSelectors = new LinearLayout(this);
        dynamicsSelectors.setOrientation(LinearLayout.HORIZONTAL);
        dynamicsChannelButton = button("CHANNEL · FL");
        dynamicsChannelButton.setOnClickListener(v -> {
            selectedDynamicsChannel = (selectedDynamicsChannel + 1) % CHANNELS;
            refreshDynamicsSelection();
        });
        dynamicsBandButton = button("BAND · LOW");
        dynamicsBandButton.setOnClickListener(v -> {
            selectedDynamicsBand = (selectedDynamicsBand + 1) % 3;
            refreshDynamicsSelection();
        });
        dynamicsSelectors.addView(dynamicsChannelButton,
                new LinearLayout.LayoutParams(0, dp(48), 1.0f));
        dynamicsSelectors.addView(dynamicsBandButton,
                new LinearLayout.LayoutParams(0, dp(48), 1.0f));
        root.addView(dynamicsSelectors);

        compressorEnabledButton = button("SELECTED COMPRESSOR: OFF");
        compressorEnabledButton.setOnClickListener(v -> {
            float[] selected = compressorSettings[selectedDynamicsChannel][selectedDynamicsBand];
            selected[0] = selected[0] == 1.0f ? 0.0f : 1.0f;
            refreshDynamicsSelection();
            applyDynamicsProfile();
        });
        root.addView(compressorEnabledButton);

        String[] compressorNames = {"Threshold", "Ratio", "Attack", "Release", "Knee", "Makeup"};
        int[] compressorMax = {600, 190, 4999, 1999, 240, 240};
        for (int control = 0; control < compressorBars.length; control++) {
            final int parameter = control;
            compressorLabels[control] = text(compressorNames[control], 14, Color.WHITE);
            root.addView(compressorLabels[control]);
            compressorBars[control] = new SeekBar(this);
            compressorBars[control].setMax(compressorMax[control]);
            compressorBars[control].setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    updateCompressorLabel(parameter, compressorValue(parameter, value));
                }
                @Override public void onStartTrackingTouch(SeekBar bar) {}
                @Override public void onStopTrackingTouch(SeekBar bar) {
                    compressorSettings[selectedDynamicsChannel][selectedDynamicsBand][parameter + 1] =
                            compressorValue(parameter, bar.getProgress());
                    applyDynamicsProfile();
                }
            });
            root.addView(compressorBars[control]);
        }
        refreshDynamicsSelection();

        Button run = button("RUN PCM TEST · 512 FRAMES");
        run.setOnClickListener(v -> runPcmTest());
        root.addView(run);

        TextView note = text("Test signal is generated locally. No microphone, speaker output, Bluetooth or system audio is accessed.", 12, Color.GRAY);
        note.setPadding(0, dp(18), 0, 0);
        root.addView(note);
        setContentView(new ScrollView(this) {{ setFillViewport(true); addView(root); }});
    }


    private float compressorValue(int control, int progress) {
        switch (control) {
            case 0: return progress / 10.0f - 60.0f;
            case 1: return 1.0f + progress / 10.0f;
            case 2: return 0.1f + progress / 10.0f;
            case 3: return 1.0f + progress;
            case 4: return progress / 10.0f;
            case 5: return progress / 10.0f - 12.0f;
            default: throw new IllegalArgumentException("Unknown compressor control");
        }
    }

    private int compressorProgress(int control, float value) {
        switch (control) {
            case 0: return Math.round((value + 60.0f) * 10.0f);
            case 1: return Math.round((value - 1.0f) * 10.0f);
            case 2: return Math.round((value - 0.1f) * 10.0f);
            case 3: return Math.round(value - 1.0f);
            case 4: return Math.round(value * 10.0f);
            case 5: return Math.round((value + 12.0f) * 10.0f);
            default: return 0;
        }
    }

    private void updateCompressorLabel(int control, float value) {
        String label;
        switch (control) {
            case 0: label = String.format(Locale.US, "Threshold · %.1f dB", value); break;
            case 1: label = String.format(Locale.US, "Ratio · %.1f:1", value); break;
            case 2: label = String.format(Locale.US, "Attack · %.1f ms", value); break;
            case 3: label = String.format(Locale.US, "Release · %.0f ms", value); break;
            case 4: label = String.format(Locale.US, "Knee · %.1f dB", value); break;
            case 5: label = String.format(Locale.US, "Makeup · %.1f dB", value); break;
            default: label = "Compressor";
        }
        compressorLabels[control].setText(label);
    }

    private void refreshDynamicsSelection() {
        String[] channels = {"FL", "FR", "RL", "RR"};
        String[] bands = {"LOW", "MID", "HIGH"};
        dynamicsToggleButton.setText(dynamicsEnabled
                ? "MULTIBAND DYNAMICS: ON" : "MULTIBAND DYNAMICS: OFF");
        dynamicsChannelButton.setText("CHANNEL · " + channels[selectedDynamicsChannel]);
        dynamicsBandButton.setText("BAND · " + bands[selectedDynamicsBand]);
        float[] selected = compressorSettings[selectedDynamicsChannel][selectedDynamicsBand];
        compressorEnabledButton.setText(selected[0] == 1.0f
                ? "SELECTED COMPRESSOR: ON" : "SELECTED COMPRESSOR: OFF");
        for (int control = 0; control < compressorBars.length; control++) {
            compressorBars[control].setProgress(compressorProgress(control, selected[control + 1]));
            updateCompressorLabel(control, selected[control + 1]);
        }
    }

    private void applyDynamicsProfile() {
        if (dsp == null) return;
        if (applyUiProfileSnapshot(createUiProfileSnapshot())) {
            status.setText("Multiband dynamics profile applied · atomic 50 ms crossfade");
        } else {
            status.setText("DSP busy or invalid dynamics profile");
        }
    }

    private static final String PROFILE_PREFS = "mt8163_audio_profiles";
    private static final String PROFILE_PREFIX = "profile:";
    private static final String AUTOSAVE_KEY = "last_session";
    private static final String[] PROFILE_CHANNELS = {"FL", "FR", "RL", "RR"};

    private float[] createUiProfileSnapshot() {
        float[] values = new float[NativeDsp.UI_PROFILE_FLOAT_COUNT];
        int p = 0;
        values[p++] = dspEnabled ? 1.0f : 0.0f;
        for (int ch = 0; ch < CHANNELS; ch++) {
            values[p++] = (gainBars[ch].getProgress() - 240) / 10.0f;
            for (int band = 0; band < 32; band++) values[p++] = eqGainsDb[ch][band];
        }
        values[p++] = (balanceBar.getProgress() - 100) / 100.0f;
        values[p++] = (faderBar.getProgress() - 100) / 100.0f;
        values[p++] = bassManagementEnabled ? 1.0f : 0.0f;
        values[p++] = crossoverHz;
        values[p++] = crossoverOrder;
        values[p++] = subGainDb;
        values[p++] = subPhaseInverted ? 1.0f : 0.0f;
        values[p++] = bassEnhancementEnabled ? 1.0f : 0.0f;
        values[p++] = bassEnhancementAmount;
        values[p++] = bassEnhancementFrequencyHz;
        values[p++] = clarityEnhancementEnabled ? 1.0f : 0.0f;
        values[p++] = clarityEnhancementAmount;
        values[p++] = clarityEnhancementFrequencyHz;
        values[p++] = dynamicsEnabled ? 1.0f : 0.0f;
        values[p++] = dynamicsLowCrossoverHz;
        values[p++] = dynamicsHighCrossoverHz;
        for (int ch = 0; ch < CHANNELS; ch++) {
            for (int band = 0; band < 3; band++) {
                for (int parameter = 0; parameter < 7; parameter++) {
                    values[p++] = compressorSettings[ch][band][parameter];
                }
            }
        }
        if (p != values.length) throw new IllegalStateException("UI profile layout mismatch");
        return values;
    }

    private void persistProfile(String key, float[] values) {
        JSONArray array = new JSONArray();
        try {
            for (float value : values) array.put((double) value);
        } catch (JSONException error) {
            throw new IllegalStateException("Cannot serialize sound profile", error);
        }
        getSharedPreferences(PROFILE_PREFS, MODE_PRIVATE).edit()
                .putString(key, array.toString()).apply();
    }

    private float[] readProfile(String key) throws JSONException {
        String encoded = getSharedPreferences(PROFILE_PREFS, MODE_PRIVATE)
                .getString(key, null);
        if (encoded == null) return null;
        JSONArray array = new JSONArray(encoded);
        if (array.length() != NativeDsp.UI_PROFILE_FLOAT_COUNT) return null;
        float[] values = new float[array.length()];
        for (int i = 0; i < values.length; i++) {
            double value = array.getDouble(i);
            if (Double.isNaN(value) || Double.isInfinite(value) ||
                    value < -100000.0 || value > 100000.0) return null;
            values[i] = (float) value;
        }
        return values;
    }

    private boolean applyUiProfileSnapshot(float[] values) {
        if (values == null || values.length != NativeDsp.UI_PROFILE_FLOAT_COUNT ||
                dsp == null) return false;
        for (float value : values) {
            if (Float.isNaN(value) || Float.isInfinite(value)) return false;
        }
        if ((values[0] != 0.0f && values[0] != 1.0f) ||
                (values[135] != 0.0f && values[135] != 1.0f) ||
                (values[139] != 0.0f && values[139] != 1.0f) ||
                (values[140] != 0.0f && values[140] != 1.0f) ||
                (values[143] != 0.0f && values[143] != 1.0f)) return false;
        for (int ch = 0, index = 1; ch < CHANNELS; ch++) {
            float gain = values[index++];
            if (gain < -24.0f || gain > 12.0f) return false;
            for (int band = 0; band < 32; band++) {
                float db = values[index++];
                if (db < -18.0f || db > 18.0f) return false;
            }
        }
        float preflightBalance = values[133], preflightFader = values[134];
        int order = Math.round(values[137]);
        if (Math.abs(preflightBalance) > 1.0f || Math.abs(preflightFader) > 1.0f ||
                values[136] < 40.0f || values[136] > 400.0f ||
                values[137] != order ||
                (order != 2 && order != 4 && order != 6 && order != 8) ||
                values[138] < -24.0f || values[138] > 12.0f ||
                values[141] < 0.0f || values[141] > 1.0f ||
                values[142] < 40.0f || values[142] > 160.0f ||
                values[144] < 0.0f || values[144] > 1.0f ||
                values[145] < 1500.0f || values[145] > 7000.0f) return false;

        int dynamicsIndex = 146;
        float dynamicsFlag = values[dynamicsIndex++];
        float lowCrossover = values[dynamicsIndex++];
        float highCrossover = values[dynamicsIndex++];
        if ((dynamicsFlag != 0.0f && dynamicsFlag != 1.0f) ||
                lowCrossover < 60.0f || lowCrossover > 1000.0f ||
                highCrossover < 1000.0f || highCrossover > 8000.0f ||
                highCrossover < lowCrossover * 1.5f) return false;
        for (int ch = 0; ch < CHANNELS; ch++) {
            for (int band = 0; band < 3; band++) {
                float enabled = values[dynamicsIndex++];
                float threshold = values[dynamicsIndex++];
                float ratio = values[dynamicsIndex++];
                float attack = values[dynamicsIndex++];
                float release = values[dynamicsIndex++];
                float knee = values[dynamicsIndex++];
                float makeup = values[dynamicsIndex++];
                if ((enabled != 0.0f && enabled != 1.0f) ||
                        threshold < -60.0f || threshold > 0.0f ||
                        ratio < 1.0f || ratio > 20.0f ||
                        attack < 0.1f || attack > 500.0f ||
                        release < 1.0f || release > 2000.0f ||
                        knee < 0.0f || knee > 24.0f ||
                        makeup < -12.0f || makeup > 12.0f) return false;
            }
        }
        if (dynamicsIndex != values.length) return false;

        if (!dsp.applyUiProfile(values)) {
            status.setText("Profile could not be applied · DSP transition may still be active");
            return false;
        }

        int p = 0;
        dspEnabled = values[p++] == 1.0f;
        for (int ch = 0; ch < CHANNELS; ch++) {
            float gain = values[p++];
            if (gain < -24.0f || gain > 12.0f) return false;
            gainBars[ch].setProgress(Math.max(0, Math.min(360,
                    Math.round(gain * 10.0f + 240.0f))));
            for (int band = 0; band < 32; band++) {
                float db = values[p++];
                if (db < -18.0f || db > 18.0f) return false;
                eqGainsDb[ch][band] = db;
            }
        }
        float balance = values[p++];
        float fader = values[p++];
        if (Math.abs(balance) > 1.0f || Math.abs(fader) > 1.0f) return false;
        balanceBar.setProgress(Math.max(0, Math.min(200, Math.round(balance * 100.0f + 100.0f))));
        faderBar.setProgress(Math.max(0, Math.min(200, Math.round(fader * 100.0f + 100.0f))));
        bassManagementEnabled = values[p++] == 1.0f;
        crossoverHz = values[p++];
        crossoverOrder = Math.round(values[p++]);
        subGainDb = values[p++];
        subPhaseInverted = values[p++] == 1.0f;
        bassEnhancementEnabled = values[p++] == 1.0f;
        bassEnhancementAmount = values[p++];
        bassEnhancementFrequencyHz = values[p++];
        clarityEnhancementEnabled = values[p++] == 1.0f;
        clarityEnhancementAmount = values[p++];
        clarityEnhancementFrequencyHz = values[p++];
        dynamicsEnabled = values[p++] == 1.0f;
        dynamicsLowCrossoverHz = values[p++];
        dynamicsHighCrossoverHz = values[p++];
        for (int ch = 0; ch < CHANNELS; ch++) {
            for (int band = 0; band < 3; band++) {
                for (int parameter = 0; parameter < 7; parameter++) {
                    compressorSettings[ch][band][parameter] = values[p++];
                }
            }
        }

        if (crossoverHz < 40.0f || crossoverHz > 400.0f ||
                (crossoverOrder != 2 && crossoverOrder != 4 &&
                 crossoverOrder != 6 && crossoverOrder != 8) ||
                subGainDb < -24.0f || subGainDb > 12.0f ||
                bassEnhancementAmount < 0.0f || bassEnhancementAmount > 1.0f ||
                bassEnhancementFrequencyHz < 40.0f || bassEnhancementFrequencyHz > 160.0f ||
                clarityEnhancementAmount < 0.0f || clarityEnhancementAmount > 1.0f ||
                clarityEnhancementFrequencyHz < 1500.0f || clarityEnhancementFrequencyHz > 7000.0f) {
            return false;
        }

        crossoverBar.setProgress(Math.max(0, Math.min(360, Math.round(crossoverHz - 40.0f))));
        subGainBar.setProgress(Math.max(0, Math.min(360, Math.round(subGainDb * 10.0f + 240.0f))));
        bassAmountBar.setProgress(Math.round(bassEnhancementAmount * 100.0f));
        bassFrequencyBar.setProgress(Math.round(bassEnhancementFrequencyHz - 40.0f));
        clarityAmountBar.setProgress(Math.round(clarityEnhancementAmount * 100.0f));
        clarityFrequencyBar.setProgress(Math.round((clarityEnhancementFrequencyHz - 1500.0f) / 10.0f));
        dynamicsLowBar.setProgress(Math.max(0, Math.min(940,
                Math.round(dynamicsLowCrossoverHz - 60.0f))));
        dynamicsHighBar.setProgress(Math.max(0, Math.min(700,
                Math.round((dynamicsHighCrossoverHz - 1000.0f) / 10.0f))));
        refreshDynamicsSelection();
        dspToggleButton.setText(dspEnabled ? "DSP: ON" : "DSP: OFF");
        bassToggleButton.setText(bassManagementEnabled ? "BASS MANAGEMENT: ON" : "BASS MANAGEMENT: OFF");
        bassEnhancementToggle.setText(bassEnhancementEnabled ? "BASS ENHANCEMENT: ON" : "BASS ENHANCEMENT: OFF");
        clarityToggle.setText(clarityEnhancementEnabled ? "CLARITY ENHANCEMENT: ON" : "CLARITY ENHANCEMENT: OFF");
        phaseButton.setText(subPhaseInverted ? "SUBWOOFER PHASE: 180°" : "SUBWOOFER PHASE: 0°");
        orderLabel.setText("Linkwitz–Riley · " + (crossoverOrder * 6) + " dB/oct");
        selectEqChannel(selectedEqChannel);
        status.setText("Complete profile restored · one atomic 50 ms crossfade");
        return true;
    }

    private void promptSaveProfile() {
        EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setHint("Profile name");
        new AlertDialog.Builder(this).setTitle("Save sound profile").setView(name)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Save", (dialog, which) -> {
                    String label = name.getText().toString().trim();
                    if (label.isEmpty() || label.length() > 32) {
                        Toast.makeText(this, "Enter a name (1–32 characters)", Toast.LENGTH_SHORT).show();
                        return;
                    }
                    persistProfile(PROFILE_PREFIX + label, createUiProfileSnapshot());
                    Toast.makeText(this, "Saved: " + label, Toast.LENGTH_SHORT).show();
                }).show();
    }

    private void showSavedProfiles() {
        Map<String, ?> all = getSharedPreferences(PROFILE_PREFS, MODE_PRIVATE).getAll();
        java.util.ArrayList<String> names = new java.util.ArrayList<>();
        for (String key : all.keySet()) {
            if (key.startsWith(PROFILE_PREFIX)) names.add(key.substring(PROFILE_PREFIX.length()));
        }
        java.util.Collections.sort(names, String.CASE_INSENSITIVE_ORDER);
        if (names.isEmpty()) {
            Toast.makeText(this, "No saved profiles yet", Toast.LENGTH_SHORT).show();
            return;
        }
        new AlertDialog.Builder(this).setTitle("Load sound profile")
                .setItems(names.toArray(new String[0]), (dialog, which) -> {
                    try {
                        float[] values = readProfile(PROFILE_PREFIX + names.get(which));
                        if (!applyUiProfileSnapshot(values)) {
                            Toast.makeText(this, "Profile is invalid or DSP is busy", Toast.LENGTH_LONG).show();
                        }
                    } catch (JSONException error) {
                        Toast.makeText(this, "Cannot read profile: " + error.getMessage(),
                                Toast.LENGTH_LONG).show();
                    }
                }).setNegativeButton("Cancel", null).show();
    }

    private void restoreLastSession() {
        try {
            float[] values = readProfile(AUTOSAVE_KEY);
            if (values != null && !applyUiProfileSnapshot(values)) {
                status.setText("Last session found but could not be restored");
            }
        } catch (JSONException error) {
            status.setText("Last session is damaged; using safe defaults");
        }
    }

    @Override
    protected void onPause() {
        if (dsp != null && gainBars[0] != null) {
            persistProfile(AUTOSAVE_KEY, createUiProfileSnapshot());
        }
        super.onPause();
    }

    private void applyEnhancements() {
        if (dsp == null) return;
        if (dsp.configureEnhancements(bassEnhancementEnabled,
                bassEnhancementAmount, bassEnhancementFrequencyHz,
                clarityEnhancementEnabled, clarityEnhancementAmount,
                clarityEnhancementFrequencyHz)) {
            status.setText("Bass / Clarity profile applied · 50 ms crossfade");
        } else {
            status.setText("DSP busy · wait for the current profile transition");
        }
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
