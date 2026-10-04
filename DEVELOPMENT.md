# Development

## Build

The project uses CMake with the Visual Studio 2026 generator. Microsoft Detours is included as the `third_party/Detours` Git submodule.

```text
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target fp fp_tests fp_fixture
ctest --test-dir build -C Release --output-on-failure
```

## Injection DLLs

stdio optimization is optional. The `fp_injection_dlls` target builds isolated CMake subprojects from the Detours sources for these architectures:

- x86
- x64
- ARM64
- ARM64EC

ARM32 is intentionally excluded. Use `FASTPIPE_INJECTION_ARCHITECTURES` to select a subset of these architectures.

```text
cmake --build build --config Release --target fp_injection_dlls
```

When runtime Hook support is enabled, the main program selects the DLL matching the target executable's PE architecture and injects it through Detours or direct suspended-process loading. If the DLL is missing, the architecture is unsupported, or injection fails, the process falls back to ordinary process creation so the core pipe functionality remains available.

```text
cmake -S . -B build-hook -G "Visual Studio 18 2026" -A x64 `
  -DFASTPIPE_ENABLE_RUNTIME_STDIO_HOOK=ON
cmake --build build-hook --config Release --target fp
```

Hook support is disabled by default. `FASTPIPE_STDIO_HOOK_DIR` can specify the directory containing the injection DLLs. The Hook only adjusts stdin/stdout buffering after `_setmode(..., _O_BINARY)` and does not modify stderr. Supported CRT DLLs include `msvcrt.dll`, `msvcr70.dll` through `msvcr120.dll`, `vcruntime140.dll`, and `ucrtbase.dll`. `LdrRegisterDllNotification` handles CRT DLLs loaded after the Hook.

## Internal implementation

- Uses `CreatePipe` with the configured buffer size.
- Uses `STARTUPINFOEXW` and an explicit handle list to limit inherited handles.
- Uses a Job Object to clean up child processes if `fp` exits unexpectedly.
- Builds architecture-specific stdio Hook DLLs from the Detours submodule.
- Propagates the final pipeline stage's exit code.

## Publishing

`publish.ps1` builds the x64 main executable and the x86, x64, ARM64, and ARM64EC Hook DLLs, runs the tests, and copies the release layout to `dist`:

```powershell
.\publish.ps1
```

The default output layout is:

```text
dist/
  fp.exe
  fp_stdio_hook_x86.dll
  fp_stdio_hook_x64.dll
  fp_stdio_hook_arm64.dll
  fp_stdio_hook_arm64ec.dll
  licenses/
    Detours-LICENSE
```

Use `-Configuration Debug` for a Debug publish or `-SkipTests` to omit the test run.

## Tests

The test suite covers command parsing, `FP_BUFFERSIZE` validation, binary data integrity through single- and multi-stage pipelines, EOF propagation, and exit-code propagation.

```text
ctest --test-dir build -C Release --output-on-failure
```
