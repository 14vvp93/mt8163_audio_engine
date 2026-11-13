# CD3313EO hardware and driver reference

Status: physical chip identity confirmed by the project owner as **CD3313EO**. The previous project note naming CD3310EO was incorrect and is superseded by this document.

Last reviewed: 2026-11-13.

## 1. Target hardware identity

The external audio processor fitted to the target MT8163 head unit is **CD3313EO**.

The recovered factory kernel module is named **`cd3313.ko`** and identifies itself as CD3313:
- I2C driver ID: `cd3313`
- Device Tree compatible string: `hcn,cd3313`
- I2C address constant: `0x44` (7-bit)
- Startup/probe guard write: `0x3F`, retried up to three times on transfer failure
- User-facing device node: `/dev/audiomux`
- Source compilation unit recovered from DWARF: `drivers/misc/cd3313/cd3313.c`

The physical part number, factory driver identity and datasheet now agree at the CD3313/CD3313EO family level. The exact board wiring, supply rail, I2C bus number, GPIO sequencing and external RC values must still be verified against the target firmware and PCB. Do not infer those board-specific facts from the datasheet alone.

## 2. Manufacturer documentation

Manufacturer: Wuxi I-CORE Electronics Co., Ltd.  
Device: CD3313EO  
Document number: `CD3313-AX-YX-A014`  
Revision: `2024-01-C1`

Datasheet:
https://datasheet.lcsc.com/lcsc/2206291101_Wuxi-I-core-Elec-CD3313EO_C3151976.pdf

Product listing:
https://www.lcsc.com/product-detail/audio-interface-ics_wuxi-i-core-elec-cd3313eo_C3151976.html

## 3. Functional architecture

CD3313EO is a digitally controlled four-channel analog audio processor. It is not a multichannel PCM/I2S DSP and does not replace the software PCM DSP in this project.

Documented functions:
- Three stereo analog input pairs.
- Input selection and selectable input gain.
- Stereo master volume.
- Bass and treble tone controls.
- Loudness processing.
- Four independent analog output attenuators: front-left, front-right, rear-left and rear-right.
- Output mute.
- Two-wire I2C control interface.
- External RC networks participate in tone/loudness frequency-response shaping.

Engineering role in MT8163 Audio Engine:
1. MT8163 AFE/ASoC and PCM provide the digital audio transport.
2. Audio Engine PCM DSP performs software processing on supported digital streams.
3. CD3313EO performs downstream analog source selection/tone/volume/output attenuation as wired by the head-unit board.
4. Amplifier and mute/power sequencing remain board-specific and must be independently mapped.

Avoid applying the same EQ, loudness or gain stage twice without an explicit route/gain plan. Preserve headroom and prevent clipping across the digital-to-analog boundary.

## 4. Published electrical characteristics

| Parameter | Datasheet value |
|---|---:|
| Recommended supply | 6–10 V |
| Typical supply | 9 V |
| Absolute maximum supply | 10 V |
| Operating temperature | -40 °C to +85 °C |
| Typical supply current | 20 mA |
| Maximum supply current | 35 mA |
| Recommended maximum input signal | 2 Vrms |
| Input impedance | 35–70 kΩ, typical 50 kΩ |
| Input gain | 0 to +11.25 dB, 3.75 dB steps |
| Master volume | 0 to -78.75 dB, 1.25 dB steps |
| Bass control | typically ±14 dB, 2 dB steps |
| Treble control | typically ±14 dB, 2 dB steps |
| Four-output attenuation | 0 to -38.75 dB, 1.25 dB steps |
| Mute attenuation | approximately 100 dB |
| Typical S/N | 106 dB |
| Typical THD | 0.01% under datasheet test conditions |
| Maximum I2C clock | 100 kbit/s |

These are manufacturer-published values. They are not substitutes for board-level measurements or operating limits imposed by the surrounding circuit.

## 5. Package and pin reference

The datasheet documents CD3313EO in SOP28 and SSOP28 variants. The 28-pin drawing identifies:

- Pin 1: CREF external capacitor
- Pin 2: VDD
- Pin 3: AGND
- Pins 4–5: left/right treble network
- Pins 6–7: right/left output coupling nodes (external capacitor nodes)
- Pins 8 and 12: right/left loudness network
- Pins 9–11: right stereo inputs 3/2/1
- Pins 13–15: left stereo inputs 3/2/1
- Pins 18–21: bass network
- Pins 22–25: rear-right, rear-left, front-right, front-left analog outputs
- Pin 26: DGND
- Pin 27: SDA
- Pin 28: SCL

Confirm package orientation and pin-1 marker on the actual PCB before probing or measuring.

## 6. I2C interface and address

The datasheet gives address byte `10001000b` for write:
- 8-bit write address: `0x88`
- 7-bit I2C address: `0x44`
- Maximum bus clock: 100 kbit/s

The recovered `cd3313.ko` uses 7-bit address `0x44` and sends one-byte control values using `i2c_master_send()`. This is consistent with the datasheet's address and control-byte interface.

A bus ACK proves only that a device acknowledged the transaction. It does not prove that the selected command has the intended analog effect. Verify the actual target bus and waveforms before changing driver timing or command sequencing.

## 7. CD3313 control-byte layout

The datasheet defines these command classes:

| Top bits | Function |
|---|---|
| `00xxxxxx` | Master volume |
| `100xxxxx` | Front-left output attenuation |
| `101xxxxx` | Front-right output attenuation |
| `110xxxxx` | Rear-left output attenuation |
| `111xxxxx` | Rear-right output attenuation |
| `010xxxxx` | Input selector, gain and loudness control |
| `0110xxxx` | Bass |
| `0111xxxx` | Treble |

Detailed bit fields and code-to-dB conversion are specified in the manufacturer datasheet, pages 13–16. The project's [CD3313 protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md) and [CD3313 register image](CD3313_REGISTER_IMAGE.md) document the recovered driver's software-side behavior. Keep datasheet semantics and recovered driver behavior separately traceable until every command has been correlated.

## 8. Driver implementation requirements

- Preserve the canonical kernel module filename **`cd3313.ko`**.
- Keep the I2C identity and address aligned with the verified target configuration; do not hard-code a bus number without runtime/DT evidence.
- Validate every command against the datasheet and recovered factory behavior.
- Serialize I2C transactions and handle short writes, negative errno, suspend/resume and device removal.
- Do not perform I2C transfers from atomic or PCM callback context.
- Keep hardware volume/mute ownership explicit to prevent conflicts with AudioCore and Android policy.
- Avoid audible pops: establish a verified mute/power-up sequence before changing route or gain.
- Add tests for command encoding, boundary values, invalid parameters, retries and error propagation.
- Keep the host emulator explicitly separate from physical-hardware validation.

## 9. Board-specific evidence still required

| Item | Status | Evidence needed |
|---|---|---|
| Physical marking | Confirmed by project owner as CD3313EO | Clear package photo for independent visual record |
| Datasheet family | Confirmed: CD3313EO | Manufacturer datasheet |
| I2C address | Driver uses 0x44; datasheet agrees | Target DT and passive bus capture |
| I2C bus/controller | Not established by chip datasheet | Target DT, sysfs and boot logs |
| Supply rail at chip | Datasheet recommends 6–10 V | Schematic or safe measurement |
| Reset/enable/mute GPIOs | Board-specific, not established | DTS, driver and PCB tracing |
| External RC network values | Board-specific | Schematic/PCB inspection |
| Analog input/output routing | Not fully established | Schematic and controlled signal tracing |

## 10. Sources

1. Wuxi I-CORE Electronics, CD3313EO manufacturer datasheet, `CD3313-AX-YX-A014`, revision 2024-01-C1: https://datasheet.lcsc.com/lcsc/2206291101_Wuxi-I-core-Elec-CD3313EO_C3151976.pdf
2. LCSC CD3313EO product listing: https://www.lcsc.com/product-detail/audio-interface-ics_wuxi-i-core-elec-cd3313eo_C3151976.html
3. Project static analysis: [CD3313 protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md), [CD3313 register image](CD3313_REGISTER_IMAGE.md), [CD3313 driver risk audit](CD3313_DRIVER_RISK_AUDIT.md).

This document supersedes the incorrect earlier CD3310EO identification note.
