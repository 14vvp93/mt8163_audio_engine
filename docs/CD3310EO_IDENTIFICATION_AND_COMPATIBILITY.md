# CD3310EO identification and CD3313 compatibility boundary

Status: unresolved silicon identification. This document intentionally separates the chip reported on the target board from the chip named by the recovered factory driver.

Last reviewed: 2026-11-13.

## 1. Critical distinction

The target head-unit audio processor has been identified by the project owner as **CD3310EO**.

The recovered factory kernel module is named **`cd3313.ko`** and its internal identity is **CD3313**, not CD3310:
- I2C driver ID: `cd3313`
- Device Tree compatible string: `hcn,cd3313`
- I2C address constant: `0x44` (7-bit)
- Startup/probe guard write: `0x3F`, retried up to three times on transfer failure
- User-facing device node: `/dev/audiomux`
- Source compilation unit recovered from DWARF: `drivers/misc/cd3313/cd3313.c`

These facts identify the driver target. They do **not** prove that the physical CD3310EO is register-, protocol-, pin-, voltage- or function-compatible with CD3313EO.

**Do not rename the driver to CD3310, do not change its I2C ID/address, and do not send CD3313 command bytes to the physical chip until compatibility has been established.**

## 2. Search result for CD3310EO

As of 2026-11-13, public searches for the exact strings `CD3310EO`, `CD3310` + audio processor, and I-CORE/Wuxi I-CORE CD3310 did not locate a trustworthy CD3310EO datasheet, manufacturer product page, pinout or register protocol.

The publicly available I-CORE product catalogue located during this research lists CD3312, CD3313, CD3313L, CD3314, CD3314L and CD3315 audio processors, but did not list CD3310 in the extracted catalogue text:
https://img01.71360.com/w3/d5m4z4/20240203/9d171b823a7f8aae301fb6cc1545c851.pdf

This is a search limitation, not proof that CD3310EO does not exist. It may be a custom/OEM part, a regional/legacy designation, a top-mark interpretation, or a device whose documentation is not indexed publicly. None of those possibilities is confirmed.

## 3. What is actually documented for CD3313EO (comparison reference only)

The available manufacturer datasheet is for **CD3313**, document number `CD3313-AX-YX-A014`, revision 2024-01-C1, published by Wuxi I-CORE Electronics Co., Ltd. It is not a CD3310 datasheet.

Manufacturer PDF mirror:
https://datasheet.lcsc.com/lcsc/2206291101_Wuxi-I-core-Elec-CD3313EO_C3151976.pdf

### Functional specification

- Digitally controlled four-channel analog audio processor.
- Three stereo analog inputs, plus a fourth selector code with no external input pins.
- Input selection and selectable input gain.
- Stereo volume, bass, treble and loudness processing.
- Four independent analog output attenuators: front-left, front-right, rear-left and rear-right.
- Independent output mute.
- Control interface: two-wire I2C.
- External RC networks are part of the tone/loudness frequency-response implementation.
- It is an analog audio processor, not a multichannel PCM/I2S DSP.

### Published electrical characteristics

| Parameter | CD3313 datasheet value |
|---|---:|
| Recommended supply | 6–10 V (typical 9 V) |
| Absolute maximum supply | 10 V |
| Operating temperature | -40 °C to +85 °C |
| Typical supply current | 20 mA; maximum 35 mA |
| Maximum input signal | 2 Vrms recommended |
| Input impedance | 35–70 kΩ, typical 50 kΩ |
| Input gain | 0 to +11.25 dB, 3.75 dB steps |
| Master volume | 0 to -78.75 dB, 1.25 dB steps |
| Bass control | typically ±14 dB, 2 dB steps |
| Treble control | typically ±14 dB, 2 dB steps |
| Four-output attenuation | 0 to -38.75 dB, 1.25 dB steps |
| Mute attenuation | approximately 100 dB |
| Typical S/N | 106 dB |
| Typical THD | 0.01% under datasheet test conditions |
| I2C maximum clock | 100 kbit/s |

These are manufacturer-published CD3313 values only. They must not be copied into the CD3310EO board profile.

### CD3313 package and pin reference

The datasheet documents CD3313EO in SOP28 and SSOP28 variants. Key pins in the 28-pin drawing:
- Pin 1: CREF external capacitor
- Pin 2: VDD
- Pin 3: AGND
- Pins 4–5: left/right treble network
- Pins 6–7: right/left output coupling nodes (datasheet labels IN(R)/OUT(R) external capacitor nodes)
- Pins 8 and 12: right/left loudness network
- Pins 9–11: right stereo inputs 3/2/1
- Pins 13–15: left stereo inputs 3/2/1
- Pins 18–21: bass network
- Pins 22–25: rear-right, rear-left, front-right, front-left analog outputs
- Pin 26: DGND
- Pin 27: SDA
- Pin 28: SCL

This pin map is not a CD3310 pin map.

### CD3313 I2C addressing

The datasheet's address byte is `10001000b` for write, i.e. 8-bit bus address `0x88`; the corresponding 7-bit I2C address is `0x44`. The datasheet specifies a maximum bus clock of 100 kbit/s and ACK after each received byte.

The recovered `cd3313.ko` uses the 7-bit address constant `0x44` and sends one-byte command/register values using `i2c_master_send()`. This is consistent with the CD3313 bus address and one-byte control-data model. It still does not establish CD3310 compatibility.

### CD3313 control-byte layout

The CD3313 datasheet defines the following command classes:

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

The detailed bit fields and code-to-dB conversion are in the manufacturer PDF, pages 13–16. The existing project documents [CD3313 protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md) and [CD3313 register image](CD3313_REGISTER_IMAGE.md) describe the recovered module's software-side behavior. Where the driver model and datasheet are not yet correlated, preserve both as separate evidence rather than silently reconciling them.

## 4. Why the current driver cannot yet be treated as a CD3310 driver

The existing module was statically identified as a CD3313 driver from its ELF/DWARF metadata, source-unit name, I2C ID, compatible string and embedded identifier. The physical board chip is reported as CD3310EO. The difference in part number is material.

Until the silicon identity is resolved, the following remain unknown for CD3310EO:
- manufacturer and full orderable part number;
- package and pinout;
- supply voltage and power-up sequence;
- I2C address and bus timing;
- command-byte/register map;
- source selector semantics;
- volume, tone, loudness and output attenuation transfer functions;
- mute behavior and any external mute/amplifier-enable GPIO;
- whether it is pin-/register-compatible with CD3313 or is a distinct device.

A matching I2C address alone would not establish compatibility. A successful ACK or a successful `i2c_master_send()` only proves that a device acknowledged the bus transaction; it does not prove that the command has the intended meaning.

## 5. Safe identification plan

1. Capture a sharp, straight-on macro photograph of the IC top marking and package, including the pin-1 marker. Record all line breaks and logos exactly; do not infer unreadable characters.
2. Record the PCB reference designator and trace the chip's supply, ground, SDA and SCL nets from the schematic or continuity measurements with power removed.
3. Read the target Device Tree and boot logs to identify the declared I2C controller, bus number, address and compatible string. Treat those as firmware configuration, not proof of the silicon marking.
4. Inspect the existing driver source and its exact module hash; keep the CD3313 driver in an isolated test mode.
5. If bus observation is needed, passively capture the factory boot/control traffic with a logic analyzer. Do not inject writes into an unidentified device.
6. Compare the observed transactions against the CD3313 datasheet and the recovered `cd3313.ko` behavior. Differences must be recorded rather than normalized.
7. Only after documentary or electrical evidence proves compatibility should a controlled, current-limited test be designed. Start with read-only identification if the device supports it; do not assume it does.

## 6. Current engineering decision

- Keep the kernel module's canonical filename **`cd3313.ko`** as required by the project.
- Treat its implementation and emulator as CD3313-specific.
- Treat the physical board component as CD3310EO, as reported by the project owner.
- Mark CD3310EO protocol support as **unverified / blocked on identification**.
- Do not map CD3313 register semantics onto CD3310EO or claim the current driver supports the fitted chip.

## 7. Sources

1. Wuxi I-CORE Electronics, CD3313 manufacturer datasheet, `CD3313-AX-YX-A014`, revision 2024-01-C1: https://datasheet.lcsc.com/lcsc/2206291101_Wuxi-I-core-Elec-CD3313EO_C3151976.pdf
2. I-CORE product catalogue (public catalogue; useful for part-family cross-check, not proof of absence): https://img01.71360.com/w3/d5m4z4/20240203/9d171b823a7f8aae301fb6cc1545c851.pdf
3. LCSC CD3313EO product listing: https://www.lcsc.com/product-detail/audio-interface-ics_wuxi-i-core-elec-cd3313eo_C3151976.html
4. Existing project static analysis: [CD3313 protocol reconstruction](CD3313_PROTOCOL_RECONSTRUCTION.md), [CD3313 register image](CD3313_REGISTER_IMAGE.md), [CD3313 driver risk audit](CD3313_DRIVER_RISK_AUDIT.md).

No trustworthy CD3310EO datasheet was located during this research. This document must be updated if a manufacturer document or readable chip marking becomes available.
