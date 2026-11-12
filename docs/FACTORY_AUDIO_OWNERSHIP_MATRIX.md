# MT8163 factory audio ownership matrix

Updated: 2026-11-12
Target: MT8163 / Android 9 / ARM32 / vendor kernel 4.9.117
Status: Phase 1 static ownership reconstruction

## Purpose

This document separates factory audio into control, PCM, policy, vehicle-state and hardware planes. It is the source-of-truth map for the approved unified MT8163 Audio Engine replacement. The replacement must have one authoritative owner for source state, route state, volume/mute policy, DSP state and hardware command scheduling.

Evidence labels: STATIC CONFIRMED means recovered from firmware artifacts; HOST VERIFIED and ANDROID BUILD VERIFIED refer only to CI-covered code; RUNTIME UNKNOWN and BOARD UNKNOWN require target diagnostics or physical inspection. No live hardware operation was performed for this matrix.

## 1. Ownership matrix

| Plane | Factory owner / interface | Responsibility | Replacement responsibility |
|---|---|---|---|
| Android framework | AudioFlinger, AudioPolicyService, vendor AudioPolicyManager | Framework streams, devices, patches and effect chains. Static references to /dev/audiomux and /dev/carstatus exist in supplied framework/vendor libraries. | Preserve Android 9 contracts. Replace vendor-specific control paths only after every call site has a replacement owner. |
| Primary audio HAL | audio.primary.mt8163.so; Normal, Voice, HFP/SCO, FM and capture handlers | PCM lifecycle, ALSA endpoints, mixer/device activation, format/rate conversion and call routing. MTK post-process call is in Normal playback only. | New primary HAL owns all supported routes. Do not assume one Normal callback covers FM, SCO, offload or capture. |
| CD3313 control | AudioEffect → ISourceService → SourceService → ExtAudioMuxer JNI → /dev/audiomux | Source selection, input mute/gain, master mute/volume, four analog speaker attenuators, tone controls and DSP power. | One typed hardware-command broker owned by Audio Core. Serialize commands; validate payloads; keep I/O off PCM callback. |
| CD3313 driver | cd3313.ko; misc device audiomux; I2C address 0x44 in paired DTBO | Chip register writes, source GPIOs, mute, attenuation and power sequencing. | Retain safe kernel transport if suitable, but remove duplicate policy ownership and harden ABI. Runtime device binding remains unverified. |
| Vehicle / MCU status | carstatus.ko samples ACC from GPIO86/IRQ and exposes status through /dev/carstatus; CarStatusService consumes that state. | ACC/reverse/parking/headlight/telephone-mute state. CarStatusService has two distinct ACC outputs: MCU text mute=10001/10000 via /dev/carstatus and CD3313 master-mute reason 2 via ExtAudioMuxer → /dev/audiomux. | Normalize vehicle events into Audio Core. Keep MCU display/power commands separate from audio mute arbitration; route the ACC mute reason through the Audio Core's owner/reason model. |
| FM tuner control | RadioService JNI, libfmjni.so, /dev/fm | Tuner power, frequency, seek/RDS and I2S metadata. Paired MT6627 path reports class-1 non-merge, master, 32 kHz. | Dedicated FM source adapter owns tuner lifecycle and source state. |
| FM direct audio | AudioPolicy FM AudioPatch → HAL AudioALSAFMController → ALSA FM_I2S_Playback | Direct I2S playback; the recovered start path does not contain a userspace PCM write loop. | Explicitly own this route or redesign it into PCM. A music effect cannot be claimed to cover it. |
| FM capture | AUDIO_SOURCE_FM_TUNER → AudioALSACaptureHandlerFMRadio → provider/client → FM_I2S_Capture | Software-readable FM PCM fanout. Not proven to feed the direct speaker patch. | Use as a PCM source only if the replacement deliberately selects and validates this route. |
| A2DP | Separate A2DP HAL/policy module and primary output routing | Bluetooth media; PCM16 stereo 44.1 kHz is declared in policy, while HAL also contains offload control. | Dedicated route adapter; detect negotiated format and offload state. |
| HFP/SCO | SourceService voice-state logic, CD3313 source priority, primary HAL HFP/SCO handlers and capture/echo paths | Call downlink, microphone uplink, echo reference, SCO and call mute. | Dedicated telephony route with correct clock, microphone, echo and gain handling; never treat it as A2DP. |

## 2. Framework callback serialization and the experimental effect adapter

The AOSP Android 9 AudioFlinger reference protects both EffectModule::process() and EffectModule::command() with the same EffectModule mLock. The SET_CONFIG command and the real-time effect callback are therefore serialized by the framework in the stock implementation. The apparent race from the plugin's separate enabled/active-call checks is not a confirmed defect under that contract.

The exact target vendor libaudioflinger.so is a separate binary and must be checked for vendor changes to this lock path before relying on the AOSP contract. Do not add a mutex or change the audio callback merely to address a race that has not been demonstrated. The effect adapter remains a diagnostic/temporary scaffold, not the final product integration.

## 3. Normal PCM boundary and channel topology

The primary HAL has one direct call to AudioMTKFilterManager::process(), from AudioALSAPlaybackHandlerNormal::write(). The order is DC removal → stereo-to-mono conversion when the speaker bit is present → MTK post-processing → BLI SRC → bit conversion → ALSA write. The Voice, HFP/SCO, HDMI, FM transmitter and capture handlers do not call this same wrapper at the inspected direct call sites.

The framework primary output and ordinary speaker ALSA path are both declared/configured stereo. For speaker output, the stereo pair may already have been folded to mono and duplicated before the MTK post-process callback. CD3313's four speaker-level controls are analog attenuation controls, not proof of four independent PCM channels. The physical distribution of PCM to FL/FR/RL/RR remains unresolved.

**Architecture consequence:** do not connect the four-channel DSP core to this Normal callback as if it were a proven quad PCM boundary. The unified engine needs a separately proven stereo ingress and an explicit, board-verified downstream channel-distribution strategy.

## 4. Mute arbitration recovered from firmware

CD3313 has two independent mute domains:

- Per-input mute: command 0x04 with input path, mute state and reason. Driver stores a bitmask per logical input channel.
- Global master mute: command 0x11 with mute state and reason. Driver stores a separate bitmask.
- A mute on the currently selected input may be reflected into global master mute with reserved reason 31.

The reason is a bit index; the driver sets or clears 1 << reason. It is not a reference counter. Clearing one owner's bit must never clear another owner's mute.

Recovered global master-mute call sites in the analyzed boot-carservices.vdex:

| Owner | Behavior | Reason bit |
|---|---|---:|
| CarStatusService ACC OFF | Immediate master mute | 2 |
| CarStatusService delayed ACC ON | Clear only if ACC remains ON | 2 |
| McuService SilenceMode | Set/clear silence mute | 1 |
| McuService transition branch | Temporary mute | 24 |
| SourceService Bluetooth voice state | Set/clear voice mute | 3 |

ACC behavior: carstatus.ko samples ACC on GPIO86/IRQ; CarStatusService reads the status from /dev/carstatus. On ACC OFF it immediately calls ExtAudioMuxer.setMasterMute(true, 2) through /dev/audiomux, separately sends MCU text mute=10001 through /dev/carstatus, and cancels pending delayed unmute. ACC ON schedules a 2,500 ms delayed transition; reason 2 is cleared only if ACC is still ON, with MCU text mute=10000. The two output commands are independent control planes. A new transition replaces pending work. Reimplement this as a cancellable state-machine transition, not as an unowned timer.

## 5. CD3313 logical source mapping

| Logical channel | Label | Selector | Chip input |
|---:|---|---:|---|
| 0 | RADIO | 2 | Source 3 |
| 1 | ARM | 1 | Source 2 |
| 2 | AUX | 0 | Source 1 |
| 3 | EXBT | 3 | Source 4 / no external pins |
| 4 | AUX2 | 0 | Source 1 |
| 5 | DAB | 2 | Source 3 |
| 6 / -1 | NONE | 3 | Source 4 / no external pins |

GPIO 396 distinguishes AUX/AUX2; GPIO 474 distinguishes RADIO/DAB. These are requested logical levels, not measured board voltages. The Java priority method maps HFP, A2DP and MUSIC to ARM; reverse and ONSTAR to AUX; RADIO/RDS to RADIO; AUX2 to AUX2; DAB to DAB.

CD3313 setBalance(FL, FR, RL, RR) controls four analog output attenuators. It is not a four-channel PCM interface and does not provide four independent PEQ/delay banks.

## 6. FM paths must remain distinct

1. /dev/fm: tuner and I2S metadata/control.
2. ALSA FM_I2S_Playback: direct HAL playback endpoint in the paired MT6627 non-merge configuration.
3. ALSA FM_I2S_Capture: software capture/fanout endpoint.
4. /dev/audiomux: CD3313 logical RADIO source/mute control.
5. Audio Policy FM Tuner In: patch/device abstraction; supplied system/vendor policy XML differs.

Static evidence proves these software paths coexist. It does not prove which route is audible, whether both FM audio paths are physically connected, or whether FM capture feeds the direct speaker patch. Those are runtime/board questions.

## 7. DSP driver collision

DTBO analysis shows enabled I2C2 nodes cd3313@44, ak7604@1c and fy7604@50. Supplied init scripts attempt to load cd3313.ko, ak7604.ko and fy7604.ko. All three register a misc device named audiomux; AK7604/FY7604 also request overlapping GPIO resources.

This is a static registration collision, not proof that all three chips are populated or that all probes succeed. CD3313 appears first in the observed insertion order, but the runtime owner of /dev/audiomux is unknown. Do not load alternative modules experimentally; obtain read-only boot log, module state and I2C probe evidence.

## 8. Safety and replacement gates

1. Do not send guessed or malformed CD3313 requests; native bridge framing has an unresolved extra-word discrepancy.
2. Treat CD3313 ioctl success as queued, not executed; cached status is not hardware readback.
3. Do not write commands to /dev/carstatus during discovery.
4. Keep hardware transactions out of the real-time PCM callback.
5. Keep normal media, FM direct, FM capture, A2DP, HFP/SCO and microphone capture as separately identified route classes.
6. Do not remove a factory component until the replacement owns its behavior, compatibility tests pass and rollback is available.

## 9. Phase 1 closure

- [x] Recover CD3313 Java/Binder/native control chain and command mapping.
- [x] Recover carstatus JNI text-write ABI and kernel state interface.
- [x] Recover ACC reason 2 and delayed unmute behavior.
- [x] Recover SilenceMode reasons 1/24 and Bluetooth voice reason 3 call sites.
- [x] Recover per-input and global mute bitmask domains.
- [x] Recover CD3313 logical input map and analog output attenuator semantics.
- [x] Separate FM tuner control, direct I2S and software capture paths.
- [x] Identify shared audiomux misc-device collision across three DSP drivers.
- [ ] Read-only target evidence for loaded modules and actual /dev/audiomux owner.
- [ ] Read-only process FD inventory for /dev/audiomux, /dev/carstatus and /dev/fm.
- [ ] Confirm selected Audio Policy XML and active FM AudioPatch on target.
- [ ] Confirm HFP downlink/uplink and echo-reference lifecycle on target.
- [ ] Confirm physical DSP population and FM/CD3313 wiring.
- [ ] Confirm exact kernel source/config/toolchain before driver build.

## References

- [Phase 1 audit baseline](PHASE1_AUDIT_BASELINE.md)
- [CD3313 hidden AudioEffect API](CD3313_HIDDEN_AUDIOEFFECT_API.md)
- [CD3313 ioctl ABI](CD3313_IOCTL_ABI.md)
- [CD3313 driver risk audit](CD3313_DRIVER_RISK_AUDIT.md)
- [Carstatus MCU service ABI](CARSTATUS_MCU_SERVICE_ABI.md)
- [Carservices Java audio flow](CARSERVICES_JAVA_AUDIO_FLOW.md)
- [DSP driver ownership analysis](analysis-reports/DSP_DRIVER_OWNERSHIP_ANALYSIS.md)
- [FM routing matrix](MT8163_FM_ROUTING_MATRIX.md)
- [Audio source coverage](MT8163_AUDIO_SOURCE_COVERAGE.md)
- [MTK process call-site audit](MT8163_MTK_PROCESS_CALLSITE_AUDIT.md)