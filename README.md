# GnssLogConverter

`GnssLogConverter` is a C/C++ command-line utility for converting supported GNSS ASCII logs into NovAtel OEM7-compatible binary records, with a reverse NovAtel binary-to-ASCII path.

Initial protocol scope:

- NovAtel OEM7: `RANGE`, `GLOEPHEMERIS`, `QZSSEPHEMERIS`, `GALEPHEMERIS`, `GPSEPHEM`, `BD2EPHEM`, `IONUTC`, `BD2IONUTC`.
- Unicore N4 ASCII input: `OBSVM`, `GPSION`, `BD3ION`, `BDSION`, `GALION`, `GPSEPH`, `QZSSEPH`, `BD3EPH`, `BDSEPH`, `GLOEPH`, `GALEPH`, `IRNSSEPH`.

The key cross-vendor path is direct conversion into NovAtel binary. For example:

```text
NovAtel RANGEA -> NovAtel RANGEB
Unicore OBSVMA -> NovAtel RANGEB
```

`OBSVMA -> OBSVMB` is not required.

Target-format CRC values are regenerated rather than copied from the source record, and input record order is preserved.

## Current status

Implemented in the bootstrap milestone:

- Bounds-checked little-endian primitive readers/writers.
- Unicore N4 CRC32 implementation validated against the R1.15 `GPSIONA` example.
- Unicore N4 documented header/payload utilities used to validate source layout assumptions.
- Unicore `OBSVM` field model and 40-byte native payload helper.
- NovAtel OEM7 28-byte binary header codec.
- NovAtel `TimeStatus` mapping.
- NovAtel RANGE binary payload codec.
- File-level `RANGEA -> RANGEB`, `OBSVMA -> RANGEB`, and `RANGEB -> RANGEA` conversion.
- CMake build, Visual Studio 2022 auto-detection build script, unit tests, and CI workflow.

Remaining implementation work includes the requested EPH/ION mappings and broader mixed-message support.

## Design principles

- C-style C++ with no third-party runtime dependencies.
- Explicit little-endian field reads/writes; do not serialize C/C++ structs directly.
- One canonical in-memory representation between input parsing and target encoding.
- Strict bounds checks for all binary reads and variable-length records.
- Explicit cross-vendor field mapping for units and field types.
- Project-specific exception: the 32-bit RANGE/OBSVM channel tracking status (`ch-tr-status`) is copied unchanged, bit-for-bit, for both `RANGEA -> RANGEB` and `OBSVMA -> RANGEB`, even if the two vendors document bit meanings differently.
- Windows / Visual Studio friendly build, with CMake for portability.

## OBSVMA -> RANGEB

Unicore N4 R1.15 stores several OBSVM quality fields as scaled integers, while NovAtel RANGE stores them as floating-point physical values. Conversion therefore includes:

```text
psr std x100    -> psr sigma (m)      / 100.0
adr std x10000  -> adr sigma (cycles) / 10000.0
C/N0 x100       -> C/N0 (dB-Hz)       / 100.0
```

The source 32-bit Unicore `ch-tr-status` value is written directly to the NovAtel RANGE tracking-status field without changing any bit, system value, signal-type value, validity flag, or reserved bit.

The same rule applies to native `RANGEA -> RANGEB`: parse the ASCII hexadecimal tracking-status field and write the exact 32-bit value into the binary RANGE observation record.

## Header handling

Unicore ASCII headers are parsed according to N4 R1.15 Table 7-51. `TimeRef` and `TimeStatus` follow the NovAtel conventions for this project.

The target standard NovAtel OEM7 binary header does not contain a separate `TimeRef` field. The Unicore source `TimeRef` is used to interpret/validate the source time base. `TimeStatus` is written using the NovAtel one-byte GPS reference time-status value, for example `UNKNOWN=20`, `FINE=160`, and `SATTIME=200`.

## Build

### Automatic Visual Studio 2022 build on Windows

Run from a normal Command Prompt or PowerShell; a Visual Studio Developer Prompt is not required:

```powershell
build_vs2022.bat
```

The script automatically:

1. Locates Visual Studio 2022 with `vswhere.exe`, including non-default installation paths.
2. Falls back to the standard Community / Professional / Enterprise / BuildTools locations when needed.
3. Loads the x64 MSVC environment through `VsDevCmd.bat`.
4. Locates CMake from `PATH` or the CMake bundled with Visual Studio.
5. Configures a clean x64 Release build, compiles the project, and runs CTest.

On success the executable is generated at:

```text
build\Release\GnssLogConverter.exe
```

### CMake manually

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## CLI

```powershell
GnssLogConverter.exe input_rangea.log output.bin
GnssLogConverter.exe input_obsvma.log output.bin
GnssLogConverter.exe input.bin output_rangea.log

GnssLogConverter.exe input.log output.bin --to binary
GnssLogConverter.exe input.bin output.log --to ascii
```

## References

- Unicore input layouts: *Unicore Reference Commands Manual For N4 High Precision Products V2 CH R1.15* (2026-06).
- NovAtel target layouts: OEM7 Commands and Logs documentation.

See `docs/protocol_notes.md` for implementation-specific notes.

## License

MIT.
