# Opt-in native renderer-texture release repair

Tracking: Ensrick/skyrim-mod-assistant#268, parent #262. Experimental; not
automatically enabled in ordinary builds or a claim of whole-game stability.

`SKSE_AUTOMATION_TEXTURE_RELEASE_FIX=1` enables a three-byte startup patch,
only when all 147 bytes of the verified 1.7.104 function match. Any unknown or
already-modified function is left untouched. Installation follows SKSE plugin
Load/LoadComplete and mandatory hooks; do not call it later while engine
workers run. The implementation checks protection changes, cache flush and
full-function readback and logs each result. Testing also reads the complete
live function again after loads, independently of the startup log.

Native cleanup at RVA 100F190 decrements the renderer wrapper's reference
count. On the final reference it releases SRV/resource/UAV, then erroneously
repeats resource/UAV/SRV. The observed second-SRV release crashes at100F207.
The reviewed native face-texture creator100DE60 stores a single created
resource reference and a single created SRV reference in the wrapper.
100EF50's temporary GetResource reference is released separately. The DDS
creator100E080 also stores its returned SRV directly, without a second AddRef.
External holders' references are not owned by this wrapper and do not justify
its duplicate releases. The fix skips only the second triplet; it does not
change the initial release order, reference decrement, allocator or unwind.

Build/run the dependency-free signature tests:

```
cmake -S tests/texture_release -B build-texture-release
cmake --build build-texture-release --config Release
ctest --test-dir build-texture-release -C Release --output-on-failure
```

The separate private exact-code emulator checks this header's signature
against the user's pinned executable (MD5 113faeb71fd8f62b26d0c8627299ab40)
and executes its instructions using synthetic COM ownership. No executable,
third-party texture, or user's save is distributed here. Runtime tests must
preserve source saves, isolate the profile, record each successful PostLoad,
check new crash logs, and restore the previous DLL afterward. Passing the
signature test alone does not validate D3D lifetime or accept this fix for
normal deployment. Unreviewed creation paths could still require ownership
audit; repeated reload memory observations are not a full GPU leak test.

## Optional cleanup execution observation

`SKSE_AUTOMATION_TEXTURE_RELEASE_PROBE=1` observes only the face-updater call
at43CB54, through a checked five-byte call replacement and14-byte absolute
relay. It requires the full cleanup signature (original or our patched form)
and original call bytes, checks trampoline capacity/range, memory protection,
cache flush and call readback. It can be used with the repair on or off.

The observer counts calls and logs the first8 and subsequent powers of2,
before and after forwarding the native operation **exactly once**. Wrapper
bytes are read before the native call only; nothing is read after possible
free. Diagnostic failures are contained and LastError preserved, but native
failures remain outside every catch. There are no COM method queries or
AddRef/Release count probes. A before-call wrapper-count observation is not
an atomic ownership proof; the returned marker proves this invocation returned,
not that every other cleanup caller or all renderer lifetimes are verified.
