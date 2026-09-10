# Early load-request experiment (not a production admission policy)

The hook is absent unless `SKSE_AUTOMATION_LOAD_REQUEST_PROBE=1` is set before
launch. The opt-in checks the exact original call bytes at RVA625FF5 before
installing. An optional `SKSE_AUTOMATION_REJECT_LOAD` names one exact basename
(including `.ess`) to reject case-insensitively. An unreadable name also
rejects when a rejection target is configured. No allowlist, currency
checkpoint policy, campaign recovery, or save repair is implemented here.
No save contents or mods are modified by this probe.

Signature verified against SkyrimSE.exe MD5
`113faeb71fd8f62b26d0c8627299ab40`: caller625FF5 calls627DE0 with `this`, a
pointer to a stream pointer, UInt32, UInt8, UInt8, UInt32. The stream's save
name pointer at+BB0 is observed through bounded ReadProcessMemory reads. At
entry the engine itself can return false on a null stream. False leads the
caller to627B20, which sends UI notifications; character preservation is NOT
proved by that static path. The stream remains caller-owned and is destroyed
by the existing caller. No SKSE pre/post-load messages are invented on reject.

Candidate57DEFE36 compiled forwarding: eight pushes plus188h frame, so fifth
byte read from rsp+1F0 and sixth dword from rsp+1F8; written back to outgoing
rsp+20 and rsp+28 respectively. Target call at DLL RVA1050B. These locations
apply only to that exact candidate build, not arbitrary future binaries.

```
cmake -S tests/load_request_probe -B build-load-request-probe
cmake --build build-load-request-probe --config Release
ctest --test-dir build-load-request-probe -C Release --output-on-failure
```

The 1,572,864-case test compiles the production wrapper and both declarations,
covering both byte parameters, target results, exact case-insensitive rejection,
nonmatching names and unreadable names under a configured rejection target.
Mocks replace the memory reader, logging and engine target. It checks exact
argument forwarding, no target invocation on rejection, unchanged caller data
and exact result. It also checks that paired failure handling calls the original
notifier first and consumes recovery eligibility once, only for our rejection.
The 35 UI tests compile the actual factory/message helper and deferred task;
they check menu eligibility, scoped string release and superseded generations.
The 48 installation tests check capacity before either write, failure-hook-first
ordering and unsuccessful return handling. Engine APIs are mocked; none of
these tests validates engine UI cancellation, machine-code writes or gameplay.

September10 regression: a second executable compiles the actual wrapper with
`ENSRICK_EXPERIMENTAL_SAVE_ADMISSION` defined. Its 144 cases cover admission
enabled/disabled, accepted/refused, configured veto, both native results, caller
retention/callback transfer/unknown replacement, and all three recovery modes.
The previous tests compiled out this branch and missed a native-false result
incorrectly enabling our `CancelLoading` recovery. The new test failed against
the old source at the recovery-eligibility assertion and passes after removing
that assignment. Only an early veto now schedules our recovery; native failure
notification and returned ownership continue unchanged. This does not solve
deferred callback lease cleanup or authorize production admission deployment.
Assertions are caught and reported to stderr/exit1, not unhandled exceptions.

## In-game refusal notice (experimental admission only)

An early admission veto now carries a value-owned reason code through the same
one-use thread-local failure channel and generation-stamped UI delegate. The
delegate queues Main Menu recovery where eligible, then requests the engine's
MessageBoxMenu with one OK button and no callback. It never calls Windows UI,
changes a save, or enables recovery for native false results. No raw exception,
file path, save name, or external plugin text is inserted into the message.
Engine queuing success is logged as such, not as proof of actual rendering.

New109 checks cover bounded fixed text, unknown/empty codes, every byte of the
pinned34-byte Create signature, disabled/unsupported paths, native false and
the exact five named arguments plus OK/null-terminated variadic button list.
The expanded45-case production UI test includes copied reason lifetime,
superseded generations and notification-queue failure. The144-case request test
checks reset/consume of stale reason codes; ordinary/native failures cannot
reuse an old notice. The original35-case UI count above describes the earlier
recovery-only suite, not the current expanded test.

Runtime inspection/tests use pinned1.7.104 Create RVA9625F0 (AE ID52269).
CommonLib's Create declaration and actual native argument/string-copy path
were checked. A matching prefix alone is not complete ABI or gameplay proof.
Main Menu, Journal, quickload, acknowledgement, subsequent valid load, and
normal quit require separate runtime evidence. This remains opt-in experimental
admission work, not automatic protection of the normal installed game.

Fable 5.1 independent read-only review (session
92cab6dd-ff89-4acb-8a42-ae21c423a9cc) confirmed the ABI and warned that the
failure path does not itself establish character preservation. Runtime tests
must first observe healthy cold/manual loads, then reject a healthy saved game
and verify continued usability, before testing the known-incompatible copy.
Keep the public crash issue open regardless of these narrow probe results.

Runtime September9: healthy forwarding, healthy rejection and the exact
incompatible Adventurer3 copy's manual rejection passed bounded menu/save/
reload tests. Main-menu Continue reached this hook and was rejected, but
left an unusable CharacterSelection menu (Back/Cancel did not recover it).
Therefore this diagnostic is NOT a production rejection mechanism. Passing
the mock tests does not establish safe cancellation at this boundary.

## Automatic main-menu recovery experiment

Additional opt-in `SKSE_AUTOMATION_RECOVER_REJECTED_LOAD=1` checks original
call bytes at625FFE (`E8 1D 1B 00 00`) and installs a paired failure wrapper.
It calls the original627B20 first, then schedules a UI delegate only for this
thread's deliberately rejected request. Native/unrelated failures stay native.
The UI delegate runs after native UI event processing and queues the engine's
`CancelLoading` kUpdate message for Main Menu only, with Journal closed.
The owned `BSUIMessageData` string is populated using the engine's Set_ref.
No raw menu close, engine flag write, sleep, or UI queue self-requeue is used.

Why defer: immediate enqueue after627B20 restored native input flags but left
the movie in CharacterSelection (PID29544). Deferred candidate16FA95D2 passed
two normal Continue/reject/recover/navigation cycles in PID31576 and normal
Quit to Desktop, controller0 at2026-09-09 23:14:10.772 CDT. Both log exact
Adventurer3 `.ess`, reject=1, target_not_entered; no pre/post-load messages.
No manual CancelLoading diagnostic, Back, or movie recovery invocation was
needed. This is bounded MAIN MENU evidence, not gameplay preservation.

Subsequent candidate D01E1C2A adds installation checks; local full build and
tests pass, but that exact binary has NOT been runtime-tested or installed.
The diagnostic checks available trampoline capacity (14 bytes per call)
before either edit and checks Write5Call results. If the request edit fails
after the failure edit succeeds, the latter merely forwards native failure.
This is not an atomic transaction or a claim that SafeWriteBuf reports OS
write errors: its interface returns void. Existing trampoline assertions and
unrelated SKSE hooks are not redesigned here.

A newer request invalidates a delegate before it runs. This does NOT establish
protection against a request arriving after the cancel message is enqueued but
before it is consumed. UI/request serialization and rapid overlapping requests
need further investigation. Other outstanding gates: Journal and quickload
rejection, valid load/save/reload after rejection, sustained gameplay, actual
plugin/currency admission policy and useful in-game refusal explanation.
Default installed SKSE remains CC2F98A4; no campaign repair is claimed.

## Read-only engine image observation (September10)

`SKSE_AUTOMATION_LOAD_STREAM_PROBE=1`, in addition to the existing request
probe opt-in, compares the actual buffered ESS against a read-sharing disk
open. It neither advances the stream nor changes save bytes. The exact
1.7.104 Win32FileType vtable is checked before reading fields: buffered bytes
at+BD0, size+174, position+BE0 and decompression flag+BCA. Unknown layouts,
already-decompressed buffers and sizes outside1..64MiB are only logged as
uncovered; this diagnostic does NOT make an admission decision.

Why this is not NiFile::file: the runtime's derived Win32FileType read method
at159D800 copies from+BD0 and updates+BE0/+170. The base class's FILE pointer
is not the source consumed here. Constructor159C730 reserves64MiB and installs
vtable1B521A0. Exact executable MD5 remains113faeb71fd8f62b26d0c8627299ab40.

Candidate40125953FE87C5E336C29CB148E21C71DAED4B44158F4BE0333F2759C7807EDB:
all5,550,827 bytes of the rejected Adventurer3 test copy matched atposition0,
decompressed0; Main recovered and native navigation/Quit passed (PID7180).
An admitted Save7 matched4,606,518 bytes and loaded through Continue; the
new Save8 matched4,601,612 bytes and loaded through Journal (PID32360).
Both controlled private muted runs exited normally with controller0.
The second run survived80s after the reload, including bounded engine input
delivery. This does not prove movement distance, long gameplay, campaign
recovery, a deployed policy, or save validity beyond the compared bytes.

The Windows synthetic test compiles the actual observer and exercises exact
multi-chunk comparison, mismatch, unreadable memory, size/layout/decompression
refusal, competing writer, handle release and default-off inertness. Other
platforms retain the portable request/recovery/installation regression tests.
