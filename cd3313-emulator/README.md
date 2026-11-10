# CD3313 host-side protocol emulator

This is an offline parser/emulator scaffold for the command buffer handled by the recovered `cd3313.ko` driver.

## Frame model

The queued event object starts with the original 32-bit Linux ioctl request word in little-endian order, followed by the user payload:

- direction: bits 30..31 (`1` = write, `2` = read);
- command/request ID: bits 8..15;
- size field: bits 16..29;
- copied user payload: `size_field * 4` bytes, capped at 256 bytes by the driver.

The 12 `cmd_len_list` entries are minimum size-field thresholds, not exact payload lengths. The parser enforces the recovered minimum and the actual copied payload length. It preserves argument bytes as opaque data because not every command structure is fully proven.

The dispatcher has two additional branches, `0x56` and `0xA3`, that are not in the 12-entry length table; both read at least one 32-bit argument, so the parser requires a one-word payload for them.

## Separate raw register interface

The driver's `cmd_write()` path is different: it parses a decimal text value, truncates it to one byte, and sends that byte over I²C. It is not the same format as the ioctl command frame and is not conflated with this parser.

## Current scope

Implemented:
- little-endian ioctl request-word parsing/encoding;
- write-direction validation;
- command ID and 14-bit size-field extraction;
- 32-bit-word payload sizing and 256-byte cap;
- recovered minimum-size validation;
- preservation of opaque payload bytes;
- counters and last-command state.

Not implemented yet:
- complete CD3313 register/state simulation;
- full argument validation and command semantics;
- readback request `0xF0` response modeling;
- I²C timing/electrical behavior.

See [the command dispatch reconstruction](../docs/CD3313_COMMAND_DISPATCH.md) and [the protocol reconstruction](../docs/CD3313_PROTOCOL_RECONSTRUCTION.md) for evidence and confidence labels.
