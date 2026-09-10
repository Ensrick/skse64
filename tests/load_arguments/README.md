# Load-hook argument regression (runtime 1.7.104)

The engine caller at RVA `62812F` passes six Win64 ABI arguments, including
`this`. The old hook and target declarations accepted only five. The omitted
byte is not an unused parameter: the engine target consumes it during load
validation. Its meaning is deliberately left unnamed.

## Exact binary evidence

Audited SkyrimSE.exe MD5: `113faeb71fd8f62b26d0c8627299ab40`.
These RVAs are not portable to other executable versions.

- Caller `62810C`: writes sixth argument to `[rsp+28h]` before `call 618970`.
- Target pushes seven registers, then sets `rbp = rsp-320h`.
  Consequently `rbp = entry-rsp-358h`.
- Target `618A9D`: reads byte `[rbp+388h]`, i.e. `entry-rsp+30h`, the sixth
  argument including the return address. It passes this byte to `6197D0` and
  tests it at `618ACE`.

Old installed DLL SHA256:
`240C9B5CFC5CE6D632FF931B96A91219CA71B34AE7D263CF4F688C25DF8AE707`.
Its hook at `103A0` calls the target at `10449` without initializing the
sixth outgoing stack slot.

Candidate DLL SHA256:
`CC2F98A4189E1980B216E0C10C15DBDB65827B44028FBAD11A129FF6D2094591`.
Hook `103A0` pushes eight registers and subtracts `78h` (total `B8h`).
At `10442`, it reads `[rsp+E8h]`, equivalent to entry-rsp+30h, into ESI.
At `10482`, it writes SIL to `[rsp+28h]` before calling the target at `1049C`.
The fifth pointer and register arguments are preserved as well. Other builds
may place these instructions elsewhere; re-audit their code, not just RVAs.

## Tests and scope

```
cmake -S tests/load_arguments -B build-load-arguments
cmake --build build-load-arguments --config Release
ctest --test-dir build-load-arguments -C Release --output-on-failure
```

CMake extracts the production hook body and both production declarations.
The executable checks 2,048 combinations of all byte values, target return
values, diagnostic state and registration cleanup state. It verifies the other
arguments, name handling, return value, lock balance and message ordering.
Engine collaborators are mocks; this is not an executable ABI test or evidence
that an incompatible saved Papyrus state can safely load.

The opt-in environment variable `SKSE_AUTOMATION_LOAD_ARGUMENT_PROBE=1` logs
the incoming value immediately before forwarding and logs the returned result.
It defaults OFF. It does not deny loads, clean saves, recover missing content
or suppress engine validation. Runtime crash causality remains unproven.

Independent read-only Claude Fable 5.1 review of the old declarations and exact
engine disassembly confirmed the omission and stack arithmetic. This review is
not a substitute for candidate runtime verification.
