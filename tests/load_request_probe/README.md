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

The 524,288-case test compiles the production wrapper and both declarations,
covering both byte parameters, target results, exact case-insensitive rejection,
nonmatching names and unreadable names under a configured rejection target.
Mocks replace the memory reader, logging and engine target. It checks exact
argument forwarding, no target invocation on rejection, unchanged caller data
and exact result. It does NOT validate engine UI cancellation or gameplay.

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
