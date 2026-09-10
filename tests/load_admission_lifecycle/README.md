# Experimental admission lifecycle regression

This test extracts actual lifecycle functions from LoadAdmissionRuntime.cpp,
inside their production namespace. Synthetic memory spans and contexts replace
Windows/engine objects; it does not emulate callback dispatch or game behavior.

```
cmake -S tests/load_admission_lifecycle -B build-admission-lifecycle
cmake --build build-admission-lifecycle --config Release
ctest --test-dir build-admission-lifecycle -C Release --output-on-failure
```

92 checks cover prerequisite signatures, bounded name continuity, gate/reentry
observation, event order, co-save identity, caller-retained versus transferred
streams, snapshot ownership, and stale outer-request tokens. The stale-token
regression failed the pointer-only implementation: request A finished, request
B reused the same stream address/name, and A's eventual return released B.
Matching the generation captured by Begin fixes that local ownership error.
The matrix also checks null/zero tokens, both native results, and all three
caller-pointer shapes; a current matching token still releases exactly once.

The separate load_request_probe suite compiles the actual request wrapper in
both modes, including forwarding the token captured before native execution.
Begin's full Windows validation is not mocked here as evidence of real parsing;
the runtime build and controlled copied-save tests remain separate obligations.

The inner entry now atomically captures a generation token and owned snapshot
after bounded name validation. Each context permits one inner acquisition;
Finish requires that token. A stale-inner regression failed pointer-only Finish
and passes with the generation check. Old snapshot ownership survives context
replacement without borrowing the new lease. Tests also reject absent, invalid,
already consumed or unreadable inputs without claiming them.

An additional 103 checks compile the actual LoadGame_Hook and reader preparation
functions. Same-generation reentry and a newer context facing an occupied reader
both refuse without closing the outer reader or clearing its save name. Restoring
the original erroneous ClosePreparedLoad on the generated refusal path makes
this test fail; regenerating the fixed production body restores PASS. The matrix
checks both native results, runtime opt-out, exactly one native call, sixth-arg
forwarding, paired Pre/PostLoad and balanced lock entry/exit. Native callbacks,
memory ownership and the reader itself are synthetic, not gameplay proof.

This is **not** complete ABA protection before inner acquisition. The initial
stream address/name still depends on native allocation lifetime assumptions.
Destruction coverage, deferred resumes, callback cancellation, and production
admission deployment remain unresolved. No timeout/address polling or fail-open
fallback is introduced by the token change.
