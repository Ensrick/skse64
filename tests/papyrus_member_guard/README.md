# Papyrus object-member bounds guard

Default ON after controlled opt-in reproduction tests. The SKSE.ini setting
`[General] EnablePapyrusMemberBoundsCheck=0` disables it for diagnosis; no INI
edit is required normally. It installs paired source/destination resolver
checks for the pinned1.7.104 executable (MD5
`113faeb71fd8f62b26d0c8627299ab40`). This addresses a reproduced out-of-bounds
member access on an empty runtime placeholder for a removed script. It does
not restore that script, clean a save, initialize currency accounting, or
certify an old campaign.

Source resolver14D54C0 is bool(tasklet, operand, Variable*). Destination
resolver14D58F0 is bool(tasklet, operand, bool*, bool*, Variable**). Both preserve
output on native failure; destination initializes its flags to false. A failed
destination resolution skips access/default/destruction in14D45C0. The outer
opcode interpreter can continue after failure: no whole-stack-abort guarantee.

Only kind7, encoded index>=2, object-typed non-null self is considered. Member
index=encoded-2; object variables start+30, each16bytes. TypeInfo own count is
bits8..17 at+20, parent pointer+10. A complete bounded parent chain permits a
known bounds verdict; cycles, unreadable/unlinked metadata and depth>64 forward
unchanged. No refcounts, script values or save bytes are changed. SEH protects
only diagnostic reads, not the original engine call. Rejects are logged with
counts and type name in SKSE, not silently reported as successful writes.

Both first-six-byte prologues consist only of whole register/stack operations.
The copied prologues run in a20byte forwarder (six original bytes plus indirect
absolute jump to original+6). Exact15/16byte signatures and capacities28/40
are checked before installing. Source+destination cache ranges are flushed.
No detour on mismatched signatures. Trampolines use the existing SKSE pools.

Host tests cover the exact empty-placeholder/index5 decision, valid25variable
objects, parent counts, bounds, bitfields, special operands, non-object types,
missing/null metadata, cycles and depth. They do not prove runtime calling
conventions or gameplay; controlled copied-save tests are required separately.

Independent Fable review confirmed prologue/signature/calling-convention math.
Its speculative complaint about the type-name offset was rejected: actual
dump and CommonLib source both establish name+8, not+18. Likewise14D66D0 is
type classification, not handle validity. The code uses inspected evidence.
