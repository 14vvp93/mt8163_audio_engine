# P2 factory HMI close and singleton destruction audit

Date: 2026-11-14

Status: **OPEN — static binary contract recovered; concurrency and callback quiescence remain unproven.**

## Immutable evidence

- Factory ELF: `vendor/lib/hw/audio.primary.mt8163.so`
- SHA-256: `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`
- Firmware dataset commit: `865b6efaeff0c4547dab1f4456f2fa664b3611d1`
- Workflow run: [43582913834](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43582913834)
- Audited feature commit: `181b97a019a8e7e9ae556a0c15783d6e439f4d7b`
- Artifact: `mt8163-primary-hal-p2-evidence`, ID `14109605571`
- Artifact ZIP SHA-256: `67023ea4a319775683bd5b8337b417d6aa6030f1c35f2e3e6fe95914fc911e84`
- Reports: `p2-factory-device-close.txt`, `p2-factory-relocations.txt`
- Workflow result: all three jobs succeeded.

## Factory object creation path

The pinned ELF symbol table and disassembly establish:

1. `AudioMTKHardwareInterface::create()` is a 4-byte thunk at `0x000d127c`.
2. Its sole instruction branches to `AudioALSAHardware::GetInstance()@plt`.
3. `AudioALSAHardware::GetInstance()` is at `0x000d0ee0`, size 868 bytes.
4. The function initializes a local static guard and an `alock` object, registers an `__cxa_atexit` callback during guard initialization, then locks the singleton-control lock.
5. Under the lock-success path it reads a global pointer; when null, it allocates `0x3cc` bytes, invokes the `AudioALSAHardware` constructor, and stores the new pointer globally. It unlocks before returning the pointer.
6. If `alock_lock_ms` reports failure, the function logs a warning and falls through into the global-pointer read/create path rather than aborting. Thus singleton creation is not fail-closed under lock-acquisition failure.

The registered `__cxa_atexit` callback's exact target and object are not identified by this report. Do not assume it is the allocated `AudioALSAHardware` instance's destructor.

## Vtable and HMI.close dispatch

The APS2 relocation audit identifies the exact vtable:

- Symbol: `_ZTVN7android17AudioALSAHardwareE`
- Demangled: `vtable for android::AudioALSAHardware`
- Address: `0x00190a84`
- Size: 236 bytes
- Itanium ABI address point: `vtable + 0x08 = 0x00190a8c`

The relocation entries resolve:

| Vtable offset | Target | Meaning |
|---:|---:|---|
| `+0x008` | `0x000c56e4` | non-deleting destructor |
| `+0x00c` | `0x000c5874` | deleting destructor |

The pinned HMI.close wrapper at `0x0009ba48..0x0009bae4`:

1. Reads the legacy object pointer from wrapper offset `+0xdc`.
2. Decrements the global HMI wrapper reference count while holding its pthread mutex.
3. On the zero-reference path, reloads the legacy object pointer.
4. Loads the object's vptr, then loads the function pointer at vptr byte offset `+0x04`, and invokes it with `blx`.
5. Clears wrapper offset `+0xdc`, unlocks, frees the wrapper, and returns zero.

Because the object's vptr is the Itanium address point at `0x00190a8c`, the invoked slot is absolute vtable offset `+0x00c`, resolving to `AudioALSAHardware::~AudioALSAHardware()` deleting thunk at `0x000c5874`. The thunk calls the non-deleting destructor through PLT and then calls `operator delete(void*)`.

**Conclusion:** HMI.close does not invoke a legacy `common.close` method here. On the last HMI wrapper reference, it invokes the `AudioALSAHardware` deleting destructor and releases the allocated object.

## Destruction behavior relevant to callbacks

The non-deleting destructor at `0x000c56e4`, size 400 bytes:

- rewrites the object's vptr to the base/destruction vtable;
- performs an indirect virtual cleanup through the object pointer at `this+0x04`, using that subobject's vtable slot `+0x04`;
- deletes the allocation referenced by `this+0x39c`, if non-null;
- clears three global pointer locations;
- destroys the callback vector at `this+0x3b0` using `VectorImpl::finish_vector()` and `VectorImpl::~VectorImpl()`;
- calls `alock_free()` for the lock at `this+0x3ac`;
- finishes and destroys the separate sorted vector at `this+0x384`.

No explicit callback admission closure, callback-vector clear loop, or in-flight callback drain is visible in this destructor body. The virtual cleanup at `this+0x04` remains unresolved and could have relevant behavior; its semantics must not be guessed.

The global-pointer identity can be correlated across both functions. In GetInstance(), the load at `0x000d0fa0` resolves through the PC-relative literal to the global pointer cell at `0x0019a178`; the store at `0x000d0fe0` writes the newly constructed object to that same cell. In the destructor, the clear at `0x000c575c..0x000c5760` resolves to `0x0019a178` and stores null. Therefore the destructor explicitly resets the singleton pointer used by GetInstance(). The two following destructor clears target `0x0019a17c` and `0x0019a180`; their ownership is not yet identified.

## Concurrency risk surface

Two distinct synchronization domains are visible:

- `GetInstance()` uses an `alock` object around singleton pointer read/create on the successful-lock path.
- HMI.open/HMI.close use a separate pthread mutex and global wrapper reference count.

HMI.open obtains the legacy object through `create()` before it increments the wrapper reference count. HMI.close may invoke the deleting destructor when that count reaches zero. This creates a candidate open-versus-last-close interleaving unless the Android HAL loader or another owner serializes open/close. The current static evidence does not establish whether such external serialization exists.

The destructor body does not visibly acquire the singleton-control `alock` before clearing global pointers and destroying the callback vector/lock. Concurrent `GetInstance()` and destruction therefore remain a risk to resolve, not a proven runtime race.

The wrapper refcount only protects the HMI wrapper lifecycle. It does not itself prove that callback dispatch has stopped, that the detached Voice Unlock worker has exited, or that every callback cookie owner remains alive until dispatch completes.

## Automated regression gate added

Commit `f95723ca926a321853ca105989c3bb4d1dd11de2` adds `scripts/repository/audit_primary_hal_p2_device_close_contract.py`.

Commit `81faa53b852de1fd64ac4cc429553874bef986f3` adds it to the P2 workflow. The new check:

- verifies the factory ELF SHA-256;
- verifies the exact vtable symbol address and size;
- verifies APS2 relocation entries for both destructor slots;
- verifies HMI.close's indirect `vptr+4` call sequence;
- verifies the create thunk branches to `GetInstance@plt`;
- preserves a dedicated report as `p2-factory-device-close-contract.txt`.

The check has been committed but its first CI execution is pending. Do not report it as passing until the new workflow run completes successfully.

## Remaining hard gates

- [ ] Resolve the virtual cleanup call through `this+0x04` and its transitive teardown semantics.
- [ ] Map the three destructor-cleared global pointer locations to their exact symbols, especially the singleton pointer used by `GetInstance()`.
- [ ] Resolve the `__cxa_atexit` callback registered during `GetInstance()` guard initialization.
- [ ] Prove whether Android's HAL loader serializes HMI.open against last HMI.close; otherwise model and test the interleaving.
- [ ] Prove singleton lock acquisition failure behavior is handled safely; current code falls through unlocked.
- [ ] Prove callback admission is closed and all in-flight callbacks are drained before the callback vector and lock are destroyed.
- [ ] Resolve Voice Unlock worker shutdown and callback interactions before deleting the hardware object.
- [ ] Add deterministic host tests for open/last-close, concurrent GetInstance/destructor, callback dispatch/destruction, and reentrancy.

Primary HAL remains fail-closed and `installable: false`. No device writes, flashing, or hardware operations are authorized. P2 remains **OPEN**.
