# ExtAudioMuxer JNI ↔ CD3313 ioctl ABI audit

Date: 2026-11-12  
Binary: `lib/lib/libcarservices_jni.so`  
Related kernel module: vendor `cd3313.ko`, Build ID `e08637c02ef9a8c5077bb9ee12fd4b76dfb91d56`

## 1. JNI method registration

The JNI native-method table registers:

- class: `android/sourceservice/ExtAudioMuxer`
- method: `SendCmdAudioMux`
- signature: `(I[I)I`
- native function pointer: Thumb address `0x003a54c9` (code begins at `0x003a54c8`)

The native function obtains the Java int-array length and its element pointer through the JNI table, logs/formats the supplied words, constructs an ioctl request, invokes libc `ioctl`, then releases the array elements with `JNI_ABORT`.

## 2. Confirmed size mismatch

The JNI function constructs the ioctl command using:

```c
ioc = 0x40000000
    | ((array_length + 1) << 16)
    | (command_type << 8);
ioctl(audio_mux_fd, ioc, int_array_elements);
```

The encoded `_IOC_SIZE` is therefore `array_length + 1` words.

The kernel driver's `cmd_ioctl` extracts that size and performs:

```c
copy_from_user(kernel_payload, user_pointer, _IOC_SIZE * sizeof(int));
```

The pointer passed by JNI refers to the Java array's `array_length` elements, but the kernel copies `array_length + 1` elements. That is a **four-byte over-read beyond the Java int array**.

The kernel prepends the ioctl command header to its own allocated command buffer before copying the user payload. The extra word is not needed to carry that header. It can be stale adjacent memory from the JNI array backing storage and is subsequently present in the asynchronous command buffer.

The exact impact depends on each command's payload layout. It compounds the kernel callback's short-payload validation gaps documented in [CD3313 ioctl and character-device ABI audit](CD3313_IOCTL_ABI_AUDIT.md).

## 3. Why the mismatch matters

- The JNI method reports the Java array length, but the ioctl size advertises one more integer.
- The kernel copies the advertised size, not the Java array length.
- A short/edge-position array can cause the extra read to cross an allocation or page boundary; that may fail the ioctl with `EFAULT`.
- If the extra bytes remain readable, an unintended word is copied into the kernel command buffer. Commands that do not validate their required payload length may consume it.
- Because the kernel queues commands asynchronously, the JNI return value does not confirm that the DSP operation completed.

This is a caller/callee ABI mismatch, not merely a kernel-only validation problem.

## 4. Required compatibility fix

Before changing the existing ioctl ABI, inventory all Java/Kotlin callers and their array conventions. The compatible fix should make the JNI encoded size exactly match the number of valid integers passed to the kernel, and make the kernel validate the command-specific minimum/maximum lengths before queuing.

For a future ABI, prefer a fixed-width versioned header containing command ID, payload byte count and version, followed by a bounded payload. Do not infer payload size from a shifted ioctl command field without validating it against the actual user buffer.

## 5. Other JNI linkage evidence

The same native class references:

- property `ro.hw.dsp.chip`;
- device node `/dev/audiomux`;
- log string `ExtAudioMuxer chip [%d] : [%s]`.

The property is not present in the imported `build.prop` or `default.prop`; it may be provisioned elsewhere and remains an open board-selection question.

## Scope

The finding is based on static Thumb disassembly, JNI method registration data and the matching kernel ioctl implementation. No Java code was executed, no device was accessed, and no ioctl or hardware write was issued.
