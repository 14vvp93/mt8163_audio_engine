# MT8163 DSP driver ownership analysis

Date: 2026-11-12  
Scope: `boot.img`, `dtbo.img`, `init.mt8163.rc`, `factory_init.rc` and the three vendor DSP modules.

## 1. Base device tree and overlay

The base DTB embedded in `boot.img` is at offset `0x6b06f8`, size 42,614 bytes. It declares three MediaTek I2C controllers, all disabled in the base tree:

| Controller | Address | Base status |
|---|---:|---|
| `/soc/i2c@11007000` | `0x11007000` | disabled |
| `/soc/i2c@11008000` | `0x11008000` | disabled |
| `/soc/i2c@11009000` | `0x11009000` | disabled |

The Android DTBO table has one entry (declared total size 42,658 bytes). Its `__fixups__` maps symbol `i2c2` to `/fragment@11:target:0`. The overlay enables the third controller and places the following nodes under it:

| Node | Compatible | I2C address | Status |
|---|---|---:|---|
| `cd3313@44` | `hcn,cd3313` | `0x44` | okay |
| `ak7604@1c` | `hcn,ak7604` | `0x1c` | okay |
| `fy7604@50` | `hcn,fy7604` | `0x50` | okay |

These are not three unrelated string hits: the DTBO fixup explicitly places all three DSP nodes in the same I2C2 overlay fragment.

## 2. Init insertion order

Both supplied init scripts load the modules in this order:

1. `carstatus.ko`
2. `cd3313.ko`
3. `ak7604.ko`
4. `fy7604.ko`

The normal boot script uses AK7604 gain fallback `-2,-2,2,0`; the factory init script uses `-2,-2,1,0`. FY7604's fallback is the same in both.

## 3. Shared misc-device registration

ELF symbol/relocation inspection confirms that each of `cd3313.ko`, `ak7604.ko` and `fy7604.ko` has its own `audiomux_misc_device` structure. Each structure's name pointer resolves to the literal `audiomux`. Each driver calls `misc_register` from its I2C probe path and checks the return value; registration failure causes that probe to fail.

Linux misc devices are registered in the shared `misc` class. Reusing the same class-device name creates a name collision. Therefore:

- If only one of the three physical chips responds, one driver can own `/dev/audiomux` and the other probes may fail their chip-detection checks.
- If two or more chips respond, their probes compete for the same misc-device name. The first successful registration can prevent later probes from completing.
- The init order makes CD3313 the first candidate to claim the node, but the actual probe timing and physical chip presence must be confirmed from runtime logs.

AK7604 and FY7604 additionally request overlapping GPIO lines (global GPIO values observed in their module disassembly: `0x182`, `0x199`, `0x189`). This supports the possibility that they are alternative board variants, although the DTBO enables both nodes.

## 4. What this proves — and what it does not

**Proven statically**
- all three DSP nodes are declared enabled in the active DTBO overlay;
- all three modules are inserted by the supplied init scripts;
- all three modules use the same misc-device name;
- each probe treats misc registration failure as a probe failure;
- AK7604 and FY7604 request overlapping GPIO resources.

**Not proven without target logs**
- which physical chips are actually populated;
- which I2C probes succeed or fail on the user's head unit;
- which module successfully creates `/dev/audiomux` at runtime;
- whether some nodes are generic placeholders for optional board variants.

A read-only boot log containing `cd3313`, `ak7604`, `fy7604`, `misc_register`, `audiomux` and I2C probe messages would settle the runtime branch. No live device access has been performed.

## 5. Design implication for MT8163 Audio Engine

Do not preserve three independent drivers that all race to own one misc node. First determine the actual fitted topology. Then choose one of these explicit designs:

- **Single-chip board:** select and load only the detected chip driver; disable unused DT nodes or ensure they fail cleanly before misc registration.
- **Multi-chip board:** give each transport a unique device identity and put a single broker above them, or use one composite driver that owns the common ABI and dispatches to chip-specific adapters.

The replacement engine should have one state owner, one serialized DSP command executor and explicit per-chip capabilities. It must not silently assume that all DTBO-enabled nodes can share a single `/dev/audiomux` registration.
