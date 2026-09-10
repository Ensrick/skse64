# Experimental stream lifetime observation

This diagnostic is compiled only with experimental save admission and installed
only when `SKSE_AUTOMATION_LOAD_STREAM_LIFETIME_PROBE=1`. It is not a lease
release fix or a supported production admission policy.

The two exact 1.7.104 destructor entries are checked against 17/16 native bytes.
Only the first six bytes (whole, position-independent instructions) are moved
to each forwarder. The common base destructor is not intercepted. Every hook
forwards the original arguments once; diagnostic exceptions must not veto it.
Tests cover forwarder encoding, argument/result preservation, exception
boundaries, trampoline capacity and bounded output. They do not execute native
machine code and are not evidence of engine callback cancellation coverage.

The output is capped at 128 observations plus an exhaustion marker. Tests must
use a fresh process; absence after exhaustion is not evidence of absence.
Pending snapshots take a nonblocking gate; contention/reentry means unknown.
Sequence numbers for acquired snapshots are assigned under the same gate as
release events. They order the snapshot, not completion of destruction. A
matched pointer is not an allocation identity and cannot establish ABA safety.
Logs may interleave between threads; incomplete lines are not reliable proof.

Build this directory with CMake and run CTest. The separate actual-source
`load_admission_lifecycle` suite exercises snapshot/reentry/ownership paths.
