![testbuild](https://github.com/ianpatt/skse64/workflows/testbuild/badge.svg)
## Building
```
git clone https://github.com/ianpatt/common
git clone https://github.com/ianpatt/skse64
cmake -B common/build -S common -DCMAKE_INSTALL_PREFIX=extern common
cmake --build common/build --config Release --target install
cmake -B skse64/build -S skse64 -DCMAKE_INSTALL_PREFIX=extern skse64
cmake --build skse64/build --config Release
```
Solution will be generated at skse64/build/umbrella.sln.

## Automation-safe fork behavior

When `SKSE_AUTOMATION_SILENT_UI=1` is present in the launch environment, the
core redirects SKSE plugin imports of `MessageBoxA` and `MessageBoxW` to the
SKSE log. It records the caption and message, then returns a conservative
non-affirmative response. Ordinary user launches are unchanged.

Builds are side-effect free by default. Visual Studio post-build deployment is
enabled only when both `SkseDeployOnBuild=true` and an explicit
`Skyrim64Path` are supplied. Before deployment, validate the CMake-produced
core without loading it into a process:

```powershell
./scripts/verify-automation-safe-core.ps1 `
  -DllPath ./build/skse64/Release/skse64_1_7_99.dll
```

## Pinned save-admission release candidate

An explicit `-DENSRICK_SAVE_ADMISSION_RELEASE=ON` superbuild also requires
`-DENSRICK_SAVE_ADMISSION_SOURCE_DIR=/path/to/skyrim-mod-assistant/mods/save-load-admission/native`.
It builds always-on compatibility admission for Skyrim 1.7.104: manual Continue,
Journal and quickload use the same guard without automation activation variables.
Ordinary builds with neither option remain unchanged. This is not an old-save
migration or a general stability guarantee.

The paired currency DLL must export `EnsrickCurrency_GetAdmissionFingerprintV1`
and successfully initialize its exact current configuration. Missing/zero
readiness, incompatible saves or unavailable stream-lifetime coverage refuse
loading. Request/recovery hook installation is mandatory; installation failure
is logged and inner admission remains fail-closed, with native error UI only
if the outer hook could not be installed. Do not treat that unsupported stack
as a working release.

Release candidates compile out the named outer and inner deliberate-veto test
switches. Read-only diagnostics remain opt-in/off by default. The native stream
and immutable co-save lease survive supported callbacks and retire before
destruction; retry tokens receive a new generation. A compatibility refusal is
not permission to restore a saved Creations order or removed mods.

Use the pinned release-build workflow and actual clean-process acceptance before
deployment. Keep original saves untouched, and distribute source/notices for the
SKSE fork, original admission library and pinned zlib separately from third-party
mod assets. The currency DLL has its own CommonLib licensing requirements.
