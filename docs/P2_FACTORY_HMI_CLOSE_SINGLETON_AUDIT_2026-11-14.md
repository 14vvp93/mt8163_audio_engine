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

The global-pointer identity can be correlated across both functions. In GetInstance(), the load at `0x000d0fa0` resolves through the PC-relative literal to the global pointer cell at `0x00192b78`; the store at `0x000d0fe0` writes the newly constructed object to that same cell. In the destructor, the clear at `0x000c575c..0x000c5760` resolves to `0x00192b78` and stores null. Therefore the destructor explicitly resets the singleton pointer used by GetInstance(). The two following destructor clears target `0x00192b7c` and `0x00192b80`; their ownership is not yet identified.

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


## Follow-up: resource-manager singleton and second destruction edge

Evidence captured by workflow run [43584659206](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43584659206), commit `25d31c459f46360c5b12ba083af2b061ffa2cf82`, artifact ID `14110150942`:

- All three workflow jobs completed successfully.
- Factory ELF SHA-256 remains `112744a4dad30d809d638d8aecd75ddba1ed7bbfcda8c4d2f9fde4463d8bfde4`.
- `AudioALSAHardware::AudioALSAHardware()` calls `AudioALSAStreamManager::getInstance()` and stores the result at `this+0x04`.
- `AudioALSAHardware::~AudioALSAHardware()` loads that member and invokes its second virtual slot.
- Resource-manager vtable `_ZTVN7android22AudioALSAStreamManagerE` has non-deleting destructor at `+0x008` / `0x00077b58`, and deleting destructor at `+0x00c` / `0x00077e20`.
- The deleting destructor at `0x00077e20` calls the non-deleting destructor through PLT, then branches to `operator delete(void*)@plt`. This proves the virtual cleanup releases the manager allocation after its non-deleting destructor returns; it does not yet establish what resources the non-deleting destructor tears down.
- `AudioALSAStreamManager::getInstance()` at `0x00076618`, size 656, lazily allocates `0xF4` bytes when its global pointer is null. The PC-relative load/store pair resolves to singleton pointer cell `0x001929A0`.
- The function initializes a local-static guard and an `alock` lock object. On `alock_lock_ms()` failure it logs a warning and falls through into the singleton pointer read/create path. Therefore this factory singleton accessor also has a statically confirmed lock-failure path that proceeds without confirmed lock ownership.
- The resource-manager vtable relocation report and HMI close contract establish the destructor dispatch chain, but no runtime race or use-after-free is claimed.

The run's audit script did not yet emit the non-deleting destructor body or decode the `__cxa_atexit` target. A follow-up CI change now adds both disassemblies and records the manager singleton pointer cell. The exact manager teardown semantics and the atexit callback target remain pending until that follow-up run passes and its artifact is inspected.

## Current safety interpretation

The factory path contains two nested lifetime domains: the HMI-owned `AudioALSAHardware` singleton and the `AudioALSAStreamManager` singleton retained by each hardware object at `+0x04`. The last HMI close statically dispatches the hardware deleting destructor, which in turn dispatches the manager deleting destructor. The lock-failure path in manager `getInstance()` is not fail-closed.

This increases the importance of proving serialization across last-close, singleton recreation, and manager destruction. It does **not** authorize replacing either factory component or enabling the replacement HAL. P2 remains **OPEN**; Primary HAL remains fail-closed and `installable: false`.


## Follow-up: manager destructor, singleton pointer, and atexit cleanup

Evidence captured from artifact `mt8163-primary-hal-p2-evidence` produced by workflow run [43584940221](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43584940221), artifact ID `14110325270`, SHA-256 `9aac3736ee6384d6d1cb4dc4f772e02a1b48293bad2083f6e462ed3f9184d6d2`:

- `AudioALSAStreamManager::getInstance()` at `0x00076618` (656 bytes) uses a local-static guard and an `alock` object. Its singleton pointer cell is `0x001929A0`; the function reads this cell and, if null, allocates `0xF4` bytes, constructs the manager, and stores the pointer.
- On `alock_lock_ms()` failure, the function logs a warning and continues to read/create the singleton without confirmed lock ownership. This is a second statically confirmed fail-open singleton path.
- The deleting destructor at `0x00077E20` calls the non-deleting destructor through PLT and then `operator delete(void*)@plt`.
- The non-deleting destructor at `0x00077B58` frees the optional buffer at `this+0xDC`, clears a separate global pointer, calls `AudioEventThreadManager::unregisterCallback(0)`, destroys the `String8`, finalizes/destroys five sorted/vector members, and frees several `alock` objects.
- The destructor body does not visibly stop or join worker threads, close callback admission, or wait for in-flight work before destroying its synchronization objects and containers. This is a static absence observation, not proof that no external owner performs such synchronization.
- The manager destructor body contains no direct reference to its singleton pointer cell `0x001929A0`. No reset of that cell is visible in the audited function. Since the destructor is invoked through `AudioALSAHardware+0x04`, a subsequent `getInstance()` may observe the old non-null pointer unless another path resets the cell. This is a high-priority stale-singleton/UAF risk; runtime occurrence is not yet proven.
- The `__cxa_atexit` registration in manager `getInstance()` passes callback address `0x00073524` and argument `0x00198FF4`. Disassembly at the callback address writes the lock object's vtable pointer and calls `alock_free()` on the supplied argument. This is consistent with cleanup of the function-local static lock object; it is not the manager object's destructor and does not visibly reset the manager singleton pointer.

The static evidence therefore establishes the manager's deletion chain and a plausible stale-pointer path. It does not establish whether another hidden/indirect path resets `0x001929A0`, nor whether a later call actually dereferences freed storage. Keep the stale singleton pointer as a blocking P2 finding until all references to the cell and lifecycle entry points are audited.

Workflow run [43584940221](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43584940221) completed successfully on all three jobs. A follow-up audit-script change was committed as `d5fa63af140f95c17ae91be192f60b4d94eb4881` to assert the atexit lock-cleanup shape and report whether the manager destructor directly references the singleton cell. Its CI result is pending.



## Correction: manager singleton reset and atexit target resolution

This section supersedes the preceding statement that the manager destructor does not reset `0x001929A0`. Reinspection of the complete non-deleting destructor disassembly shows that it **does reset the manager singleton pointer**:

- At `0x00077BAC`, it loads the PC-relative literal at `0x00077DE8`.
- At `0x00077BB0`, it sets `r1 = 0`.
- At `0x00077BB4`, it resolves the global pointer-cell address using the literal `0x0011ADE4`; ARM PC-relative arithmetic resolves this to `0x001929A0`.
- At `0x00077BB8`, `str r1, [r0]` stores null into that cell.

Therefore the earlier stale-manager-singleton-pointer conclusion is withdrawn. The destructor's reset is now a required automated regression assertion. This does not prove safe concurrent access: the reset is not visibly protected by the singleton `alock`, and the lock-failure path in `getInstance()` still proceeds without confirmed lock ownership. A concurrent get/destroy race remains a separate unresolved lifecycle concern.

The `__cxa_atexit` callback pointer passed by `AudioALSAStreamManager::getInstance()` is `0x00073524`. This address is an interior cleanup thunk within `AudioALSAHardwareResourceManager::getInstance()` (the disassembler labels it as `getInstance()+0x290`), not a standalone dynamic FUNC symbol. Its body rewrites the supplied lock object's vptr and calls `alock_free()`. It is cleanup for the function-local static lock object; it is not the manager object's destructor and does not establish manager singleton ownership.

The audit script was corrected in commit `37c6530db5f83173fa3d6b5dc5430af8964264cf` to:
- verify the exact manager singleton reset instruction sequence and PC-relative literal;
- verify the atexit cleanup thunk's `alock_free` call and lock-vptr rewrite;
- report the confirmed reset rather than infer it from a textual address in disassembly.

The previous CI failure `43585167120` was caused by an over-specific regex in the new atexit assertion; the destructor disassembly itself showed the reset sequence. The corrected script's CI run is pending.


## Follow-up: automated regression checks for both singleton lock-failure paths

The latest reproducible artifact (workflow run [43585742590](https://github.com/14vvp93/mt8163_audio_engine/actions/runs/43585742590), artifact ID `14110606564`, ZIP SHA-256 `b19cb857f03443d440931299f9e27942543c1756d80d0271a2eb90bda6c62451`) confirms both singleton accessors' fail-open branches in the pinned factory ELF:

- `AudioALSAStreamManager::getInstance()`: after `alock_lock_ms@plt` returns nonzero at `0x76674`, the function logs a warning and falls through to the singleton pointer read at `0x766d8..0x766e0`.
- `AudioALSAHardware::GetInstance()`: after `alock_lock_ms@plt` returns nonzero at `0xd0f3c`, the function logs a warning and falls through to the singleton pointer read at `0xd0fa0..0xd0fa8`.

The audit script change in commit `4f3007b45f653b1850c2e4b8607a1443244b4254` adds exact-instruction regression assertions for both branches. The new commit's CI run must pass before these new assertions are considered verified. The finding remains static: the code proceeds without confirmed lock ownership; no actual concurrent corruption is claimed.

The next lifecycle gate is to encode the callback registry's three equivalent lock-failure paths (registration, clear, dispatch) as exact static assertions, then add deterministic host-side concurrency tests for open/last-close, concurrent singleton access/destruction, callback dispatch/clear/destruction, and reentrancy.
