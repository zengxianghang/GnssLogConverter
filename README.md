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
- CMake build, Visual Studio 2022 build script, unit tests, and CI workflow.

Next implementation steps:

- NovAtel OEM7 28-byte binary header and CRC writer/reader.
- Unicore ASCII header parser.
- NovAtel time-status mapping.
- Direct `OBSVMA -> RANGEB` field conversion.
- Native `RANGEA <-> RANGEB` conversion.
- EPH/ION mappings.

## Design principles

- C-style C++ with no third-party runtime dependencies.
- Explicit little-endian field reads/writes; do not serialize C/C++ structs directly.
- One canonical in-memory representation between input parsing and target encoding.
- Strict bounds checks for all binary reads and variable-length records.
- Explicit cross-vendor field mapping; do not assume equal field types, scaling, status bits, or signal identifiers without verification.
- Windows / Visual Studio friendly build, with CMake for portability.

## OBSVMA -> RANGEB

Unicore N4 R1.15 stores several OBSVM quality fields as scaled integers, while NovAtel RANGE stores them as floating-point physical values. Conversion therefore includes:

```text
psr std x100    -> psr sigma (m)      / 100.0
adr std x10000  -> adr sigma (cycles) / 10000.0
C/N0 x100       -> C/N0 (dB-Hz)       / 100.0
```

The source Unicore tracking-status word must be translated deliberately into the NovAtel RANGE tracking-status definition. Signal type mappings must be verified per GNSS system.

## Header handling

Unicore ASCII headers are parsed according to N4 R1.15 Table 7-51. `TimeRef` and `TimeStatus` follow the NovAtel conventions for this project.

The target standard NovAtel OEM7 binary header does not contain a separate `TimeRef` field. The Unicore source `TimeRef` is used to interpret/validate the source time base. `TimeStatus` is written using the NovAtel one-byte GPS reference time-status value, for example `UNKNOWN=20`, `FINE=160`, and `SATTIME=200`.

## Build

### CMake

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Visual Studio 2022

```powershell
build_vs2022.bat
```

## Planned CLI

```powershell
GnssLogConverter.exe input.log output.bin
GnssLogConverter.exe input.bin output.log

GnssLogConverter.exe input.log output.bin --to binary
GnssLogConverter.exe input.bin output.log --to ascii

GnssLogConverter.exe input.log output.bin --vendor novatel
GnssLogConverter.exe input.log output.bin --vendor unicore
```

When the direction is not specified, the final tool will infer it from the input framing and/or filename extension when unambiguous.

## References

- Unicore input layouts: *Unicore Reference Commands Manual For N4 High Precision Products V2 CH R1.15* (2026-06).
- NovAtel target layouts: OEM7 Commands and Logs documentation.

See `docs/protocol_notes.md` for implementation-specific notes.

## License

MIT.
