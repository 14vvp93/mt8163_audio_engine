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
            restoreLastSession();
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

        Button run = button("RUN PCM TEST · 512 FRAMES");
        run.setOnClickListener(v -> runPcmTest());
        root.addView(run);

        TextView note = text("Test signal is generated locally. No microphone, speaker output, Bluetooth or system audio is accessed.", 12, Color.GRAY);
        note.setPadding(0, dp(18), 0, 0);
        root.addView(note);
        setContentView(new ScrollView(this) {{ setFillViewport(true); addView(root); }});
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
        if (p != values.length) throw new IllegalStateException("UI profile layout mismatch");
        return values;
    }

    private void persistProfile(String key, float[] values) {
        JSONArray array = new JSONArray();
        for (float value : values) array.put(value);
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
        dspToggleButton.setText(dspEnabled ? "DSP: ON" : "DSP: OFF");
        bassToggleButton.setText(bassManagementEnabled ? "BASS MANAGEMENT: ON" : "BASS MANAGEMENT: OFF");
        bassEnhancementToggle.setText(bassEnhancementEnabled ? "BASS ENHANCEMENT: ON" : "BASS ENHANCEMENT: OFF");
        clarityToggle.setText(clarityEnhancementEnabled ? "CLARITY ENHANCEMENT: ON" : "CLARITY ENHANCEMENT: OFF");
        phaseButton.setText(subPhaseInverted ? "SUBWOOFER PHASE: 180°" : "SUBWOOFER PHASE: 0°");
        orderLabel.setText("Linkwitz–Riley · " + (crossoverOrder * 6) + " dB/oct");
        selectEqChannel(selectedEqChannel);
        if (!dsp.applyUiProfile(values)) {
            status.setText("Profile queued unsuccessfully · DSP transition may still be active");
            return false;
        }
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
