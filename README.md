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
