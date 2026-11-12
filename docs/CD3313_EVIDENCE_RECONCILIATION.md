# CD3313 evidence reconciliation

Date: 2026-11-12  
Scope: static firmware analysis and host-side protocol implementation  
Status: open until the current vendor module's callback is fully re-extracted and cross-checked.

## 1. Select the correct firmware baseline

Two different cd3313.ko builds are present in the supplied firmware:

| Evidence | Standalone reference | Installed-module candidate |
|---|---|---|
| Archive path | firmware/dumps/mt8163/kernel/cd3313.ko | vendor_lib/lib/modules/cd3313.ko |
| SHA-256 | b9ef172de1532867208fc620d4fe8d24612aa2ee4ad2d62ab2daaf6622387afb | 6a96207794c770d05a9ba6981a25af9868f38ebac0391c1c6461cd20ed394594 |
| Build ID | dae15c9eb019e9388d15cb88088c202d017ebcf0 | e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56 |
| Build tree | publisher26 | publisher28 |

The supplied firmware fingerprint and init.mt8163.rc point to the vendor archive module as the stronger installed-module candidate. The standalone module is an older reference. The function comparison shows that event_callback, set_input_channel, set_master_volume and dsp_shutdown differ, and the vendor module adds bass-boost code. Therefore, the standalone command-dispatch reconstruction must not be treated as the complete specification for the installed candidate.

Primary comparison: [CD3313 module variant diff](analysis-reports/CD3313_MODULE_VARIANT_DIFF.md).

## 2. JNI-to-kernel framing is now byte-level evidence

The matching ARM32 JNI library is libcarservices_jni.so, Build ID bf391e545edd2e482b72cd8a431d1c59, SHA-256 ed93d034bfecb0e4bc975e16b64c4fc0b3f1bd631b075759f700269c049fdf1f.

The recovered native request formula is: request = 0x40000000 | ((array_length + 1) << 16) | ((command & 0xff) << 8).

The kernel interprets the size field as 32-bit words and copies that many words from the Java array pointer. This reads one 32-bit word beyond the Java array's declared length. Example: command 0x15 passes four speaker indices but requests a five-word copy; the callback consumes the first four words. The extra word is unspecified and must never be recreated by reading past the caller's array.

A replacement bridge that must preserve this legacy request shape can allocate a separate array_length + 1 buffer, copy the caller's values, initialize the final compatibility word, and pass that fully initialized buffer. Whether to retain the extra word or revise the ABI must be decided only after all callers and command thresholds are reconciled. Do not change the legacy size formula in isolation.

The host DriverCommand format models an explicit bounded driver frame and its typed builders do not silently reproduce the JNI over-read. The parser can preserve an explicitly supplied trailing word. These are intentionally separate abstractions.

Primary evidence: [JNI ABI audit](analysis-reports/EXT_AUDIO_MUXER_JNI_ABI_AUDIT.md).

## 3. Current-module dispatch differences and unresolved conflicts

The current vendor-module ABI audit records a special 0x09 callback branch that reads one payload word without a visible local minimum-length check. The older standalone dispatch map does not list 0x09. Its operation semantics are not yet recovered.

The parser now recognizes 0x09 as an opaque special command and requires one complete 32-bit word. No typed builder, emulator state mutation or hardware operation is assigned to it. This is fail-closed with respect to memory bounds, not a claim that its semantics are understood.

There is also an evidence discrepancy around command 0x03:
- the older standalone dispatch map records a three-word minimum;
- the current vendor-module ABI audit says no visible minimum-length check precedes the two argument reads;
- the general ioctl ABI reconstruction records the 12-entry length table with 0x03 = 3.

These statements must be reconciled against the exact vendor archive module's callback disassembly and branch control flow. Until then, the safe parser keeps the three-word threshold already encoded in the recovered 12-entry table. Do not lower it based on an isolated statement from another report.

Other current-module special branches 0x56 and 0xA3 require at least one complete word in the safe parser, even though the vendor callback's static analysis found no local length check.

## 4. Slab-object boundary

The recovered driver allocates a 256-byte command-cache object, writes the 4-byte ioctl request header at offset zero, then copies the payload at offset +4. The kernel caps the copy at 256 payload bytes. Therefore, 64 words exceed the nominal object by four bytes; at most 63 words (252 payload bytes) fit after the header.

The exact vendor hardened-usercopy implementation/config still requires source confirmation. The host parser rejects payloads above 252 bytes and does not model the unsafe copy.

## 5. Safe implementation status

- Every parsed frame must contain the full declared payload; no implicit read or padding is performed by the parser.
- Command 0x15 requires four complete 32-bit indices and validates each index in 0..15.
- 0x09, 0x56 and 0xA3 require one complete word but remain semantically opaque or only partially modeled.
- Frames above 63 words are rejected.
- ioctl success is treated as queue submission, not I2C completion.
- Cached state is not represented as physical readback.
- Production transport remains disabled.

## 6. Required closure work

- [ ] Re-extract the command dispatcher from the exact vendor archive module Build ID e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56.
- [ ] Reconcile every command ID, generic length-table check, local branch check, actual argument reads and command-specific ranges.
- [ ] Resolve 0x03 threshold discrepancy and 0x09 operation semantics.
- [ ] Inventory Java/Binder callers and record the exact array length and argument values for every command.
- [ ] Decide and implement the safe legacy JNI compatibility buffer, including deterministic initialization of the extra word, without modifying the original JNI binary.
- [ ] Confirm the vendor kernel's slab-cache size and hardened-usercopy behavior from exact source/config.
- [ ] Keep hardware transport detached until the above items and board ownership are proven.

No ioctl, I2C write, GPIO change, kernel module load or physical-device test was performed for this reconciliation.
