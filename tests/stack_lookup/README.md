# Active-stack lookup regression

This harness compiles the production `VMClassRegistry::GetStackInfo` body,
with only the map and lock boundary mocked. It is not a substitute for an
in-game layout/serialization test. Run with:

```text
cmake -S tests/stack_lookup -B build-stack-lookup
cmake --build build-stack-lookup --config Release
ctest --test-dir build-stack-lookup -C Release --output-on-failure
```

Cases cover 65,536 inline-array capacity patterns, direct opaque pointer
identity, absent IDs, null entries, balanced and nested locking, and no writes
to the stack. CMake tracks the production source as a configure dependency.

## Runtime diagnostic (default OFF)

Set `SKSE_AUTOMATION_STACK_LOOKUP_PROBE=1` only on an isolated test launch.
It samples a real running Stack under the VM lock and tests the production
lookup while retaining the recursive lock. Pointer and embedded ID must agree.
Logging occurs outside the spinlock; vanished/replaced stacks are NOT_COVERED,
never passing coverage. No synthetic stacks or persistent objects are created.

`legacy_first_value` deliberately reads the same **eight bytes** at Stack+0x18
which the obsolete pointer chain read. It is not just the 32-bit capacity:
adjacent bytes may also appear in that raw word. Stack+0x18 is the small-array
header, and Stack+0x80 is the ID. Neither this probe nor production consumers
decode the obsolete `VMStackInfo` fields; treat the return as opaque.

`STACK_STORAGE_SAVE/LOAD` log slot counts only when the diagnostic is enabled.
Require continued serialization, completed ESS/co-save pairs, a successful
reload, and a separate diagnostic-OFF save. A zero-active-stack save proves
no coverage. A controller exit code alone does not prove absence of a crash.

The September 9 controlled old-code run crashes on the actual lookup, while
the corrected run saves successfully using the same original fixture. The
probe deliberately forces lookup coverage, so this is not a claim that the
original user interaction timing was reproduced exactly. Evidence and scope:
https://github.com/Ensrick/skyrim-mod-assistant/issues/261
