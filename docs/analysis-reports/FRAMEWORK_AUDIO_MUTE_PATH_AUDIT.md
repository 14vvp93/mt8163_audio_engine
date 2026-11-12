# Framework audio mute path audit

Date: 2026-11-12  
Binaries: `libaudioflinger.so`, `libaudiopolicymanagerdefault.so`

## 1. Shared framework helper

The embedded `.gnu_debugdata` symbol tables identify a local helper named `android::setAudioOutputMute(bool)` in both libraries:

- AudioFlinger copy: symbol address `0x0002ebc5`, size 160 bytes.
- AudioPolicyManager copy: symbol address `0x00020a91`, size 132 bytes.

Both binaries contain the device paths `/dev/audiomux` and `/dev/carstatus`, and both helper implementations open/close the associated device descriptors. The mute helper constructs the same ioctl request in both copies:

```c
0x40040400
```

This decodes as an `_IOC_WRITE` request with type `0x04`, command number `0x00` and encoded size `4`.

## 2. Kernel-side interpretation

The CD3313 vendor module's ioctl callback dispatches type `0x04` to its input-mute handling path. That callback requires the encoded word count to be greater than two and reads three payload words. The framework helper advertises four words, so the size is sufficient for that handler; the fourth word is not read by the observed callback path.

This is a notable naming/semantic boundary: the framework helper is called `setAudioOutputMute`, while the CD3313 status/configuration strings describe command `0x04` as input mute. It may be intentional to silence output by muting DSP inputs, but the exact three-word payload and its relationship to master mute command `0x11` must be confirmed before rewriting the ABI.

## 3. Multiple command sources

The framework helper exists independently in AudioFlinger and AudioPolicyManager. The JNI `ExtAudioMuxer.SendCmdAudioMux(int, int[])` is a third caller of the same `/dev/audiomux` interface.

The kernel ioctl path queues work asynchronously. Consequently, framework mute requests and JNI commands can be accepted by the same device node without a synchronous hardware-completion result. The CD3313 driver serializes queued ioctl callbacks, but its raw character-device write path bypasses that worker.

## 4. Follow-up questions

1. Decode the exact four-word framework mute payload and determine why only three words are consumed by the CD3313 handler.
2. Compare type `0x04` handling across CD3313, AK7604 and FY7604 drivers.
3. Determine whether command `0x04` intentionally mutes all DSP inputs to implement framework output mute, or whether this is a legacy naming/behavior mismatch.
4. Reconcile framework mute/unmute with master-mute reason command `0x11` so the replacement engine has one consistent mute-state model.

Static binary analysis only. No mute command was sent to a device and no live audio state was changed.
