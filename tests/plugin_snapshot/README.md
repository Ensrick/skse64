# Loaded plugin table snapshot reader (not wired, not runtime-verified)

`skse64/LoadPluginSnapshot.h` is a header-only, C++11, portable reader that
produces the engine's loaded full and light plugin names in index order from
a caller-supplied bounded memory reader. It exists so a future early-load
admission adapter can compare a requested save's plugin table against the
RUNNING engine's tables instead of a disk profile or a reference save.
No existing hook or file is changed by this addition; nothing calls it yet.

## Pinned layout (SKSE 1.7.104 headers in this repository)

| Field | Source | Offset |
| --- | --- | --- |
| `DataHandler::modList` | `skse64/GameData.h` (STATIC_ASSERT) | 0xD60 |
| `ModList::loadedMods` (full) | `skse64/GameData.h` | 0xD70 |
| `ModList` second array (light) | `skse64/GameData.h`, labelled "loadedCCMods, just assuming" | 0xD88 |
| `tArray` entries / capacity / count | `skse64/GameTypes.h` | 0x00 / 0x08 / 0x10 |
| `ModInfo::name[0x104]` | `skse64/GameData.h` | 0x58 |
| `ModInfo::fileFlags`, `kFileFlags_Light = 1<<9` | `skse64/GameData.h` | 0x438 |
| `ModInfo::modIndex`, inactive 0xFF | `skse64/GameData.h` | 0x478 |
| `ModInfo::lightIndex` | `skse64/GameData.h` | 0x47A |

Because the 0xD88 label is an explicit guess in the pinned header, the reader
verifies the class of every entry through `fileFlags` and refuses when a
table holds the wrong class, an inactive file, or a non-sequential
`modIndex` / `lightIndex`. Zero loaded full plugins is refused as a layout
failure. Counts above 254 full or 4096 light, count above capacity, null or
unreadable pointers, unterminated, empty, control-character or path-character
names, ASCII-case duplicates across both tables, and address arithmetic
overflow all throw `LoadPluginSnapshot::SnapshotError`.

This is NOT universal layout autodetection. The caller must gate on the exact
pinned 1.7.104 executable, a dispatched DataLoaded, and a non-null
`DataHandler` singleton before calling, must supply a reader that fails
outside readable memory (for example bounded `ReadProcessMemory`), and must
catch every exception (including `std::bad_alloc` from string/vector/set
allocation) before control returns into engine frames. The header never
dereferences engine pointers, copies engine classes, or calls engine code.

Name matching policy for non-ASCII bytes is UNRESOLVED: bytes are preserved
verbatim and only ASCII letters are folded for duplicate detection. No claim
of Windows or engine case equivalence is made; the test suite asserts that
two names differing only in non-ASCII case are both admitted, so that a
future policy decision is explicit rather than accidental.

## Build and test

```
cmake -S tests/plugin_snapshot -B build-plugin-snapshot
cmake --build build-plugin-snapshot --config Release
ctest --test-dir build-plugin-snapshot -C Release --output-on-failure
```

Windows and Linux, no downloaded dependency, no game files, no installed
mods. The single executable compiles the actual header against a simulated
address space whose reader succeeds only inside mapped fixture regions. Cases
cover happy full/light including zero light and both pinned maxima, count and
capacity bounds, null and unmapped entries, truncated and unterminated nodes,
malformed names, wrong flags and indices, inactive files, ASCII-case
duplicates within and across tables, arithmetic overflow, stable repeated
output, and that neither success nor refusal writes fixture memory.

Passing synthetic tests alone does not establish live layout or thread safety.
September10: the gated request observer in candidate7AF80B2F logged77 full and
307 light plugins. Every name in index order matched independently parsed
Save7. Both Continue and F9 Quickload returned success on disposable inputs;
the latter reached625FF5 with arg2=1. This verifies those actual observations,
not a deployed admission policy or universal runtime compatibility.

Fable5.1 drafted the original reader/tests (session1118b72b-bb7e-4f51-a2cd-63da8b12f858).
Parent review corrected the no-argument Names fixture helper, required the FE
marker for light plugins, checked read-span overflow before callbacks, refused
dot-directory names, and made numeric_limits::max Windows-macro-safe.72 local
checks pass, including pinned254/4096 maxima. Full plugin compilation asserts
the actual GameData field offsets; runtime observation remains opt-in.
