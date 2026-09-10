# Admitted co-save snapshot consumer

Experimental only. The adapter reads the immutable bytes validated by the
admission lease. A shared owner pins that lease even if the pending context is
released before serialization finishes. Binding copies an owner, not file bytes;
it neither allocates a new control block nor opens/seeks a file.

The concrete serialization stream uses this adapter; nonvirtual Open/Create/
Close must not be accessed through an IFileStream base reference. The virtual
byte-count type is derived from the actual base signature (Windows UInt32 is
unsigned long, unlike std::uint32_t). Normal file operations delegate to the base.

Bounds faults are sticky, copy nothing and do not advance. Checked public
serialization functions return0/false, never pretend a failed read succeeded.
No exception crosses a third-party callback. Write attempts/invalid API phase
calls are separately counted and refused without poisoning later valid reads.
A fatal host read fault aborts remaining callbacks and is explicitly logged.
This is not a claim to repair a malformed plugin payload or its saved state.

Tests use actual adapter/header and extracted public Serialization.cpp functions
plus its pre-delete save guard. Fake filesystem calls count whether deletion or
creation was reached; no real file is deleted. Actual-source lifecycle tests
verify a snapshot can outlive pending context. Full Windows SKSE builds check
the actual IFileStream ABI; fake-file tests are not engine integration proof.

Remaining obligations: private engine tests, release packaging, native deferred
ownership/cancellation, late stream-continuity failure cleanup, and clear in-game
refusal feedback. Logging context release does NOT claim the underlying lease
closed while another snapshot owner may retain it.
