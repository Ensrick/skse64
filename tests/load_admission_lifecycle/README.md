# Experimental admission lifecycle regression

This test extracts actual lifecycle functions from LoadAdmissionRuntime.cpp,
inside their production namespace. Synthetic memory spans and contexts replace
Windows/engine objects; it does not emulate callback dispatch or game behavior.

```
cmake -S tests/load_admission_lifecycle -B build-admission-lifecycle
cmake --build build-admission-lifecycle --config Release
ctest --test-dir build-admission-lifecycle -C Release --output-on-failure
```

127 checks cover diagnostic signatures, bounded name continuity, gate/reentry
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
after bounded name validation. Each outer invocation permits one inner acquisition;
InnerReturned requires that token but retains the lease until outer ownership
or native destruction is known. A stale-inner regression failed pointer-only
cleanup and passes with the generation check. Old snapshot ownership survives context
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

Deferred transfer marks the context resumable only after the outer invocation
returns with a null caller pointer. A same-live-stream/name/vtable retry receives
a new generation and the retained immutable snapshot, never a reread of consumed
ESS bytes. It permits another single inner acquisition. Synchronous derived
destruction retires a matching context before freeing/reusing its address;
existing reader owners may keep only their own immutable byte leases alive.
An unexpected destructor under the gate permanently poisons admission for the
process, rather than deadlocking or later trusting a potentially reused address.

Tests cover native true/false results, pre-inner callback cancellation, retry,
old outer/inner tokens after retry, name mismatch, unknown pointers, reader
ownership through destruction, and gate-owned invariant failure. Mandatory live
detour and forwarder verification has its own 222-check actual-source suite in
load_stream_lifetime. The source implementation is still experimental until
native callback cancel/resume execution and packaging are verified. These tests
do not certify arbitrary foreign native calls bypassing both derived destructors,
nor prove whole-game stability. No timeout or post-free address polling is used.
