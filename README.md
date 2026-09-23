# GnssLogConverter

`GnssLogConverter` is a C/C++ command-line utility for bidirectional conversion between native ASCII and native binary GNSS receiver logs.

Initial protocol scope:

- NovAtel OEM7: `RANGE`, `GLOEPHEMERIS`, `QZSSEPHEMERIS`, `GALEPHEMERIS`, `GPSEPHEM`, `BD2EPHEM`, `IONUTC`, `BD2IONUTC`.
- Unicore N4: `OBSVM`, `GPSION`, `BD3ION`, `BDSION`, `GALION`, `GPSEPH`, `QZSSEPH`, `BD3EPH`, `BDSEPH`, `GLOEPH`, `GALEPH`, `IRNSSEPH`.

Both directions are in scope:

```text
ASCII -> Binary
Binary -> ASCII
```

The converter preserves record order and protocol-native message framing. Target-format CRC values are always regenerated rather than copied from the source record.

## Current status

Implemented in the bootstrap milestone:

- Bounds-checked little-endian primitive readers/writers.
- Unicore N4 CRC32 implementation validated against the R1.15 `GPSIONA` example.
- Unicore N4 24-byte binary header encode/decode.
- Unicore binary record CRC write/validation.
- Unicore `OBSVM` binary payload encode/decode (4-byte count + 40 bytes per observation).
- CMake build, Visual Studio 2022 build script, unit tests, and CI workflow.

Not yet enabled:

- End-to-end file conversion.
- Unicore ASCII header conversion (the R1.15 section reviewed does not document numeric binary values for `TimeRef` / `TimeStatus`; these values will not be guessed).
- Remaining Unicore EPH/ION codecs.
- NovAtel OEM7 codecs.

## Design principles

- C-style C++ with no third-party runtime dependencies.
- Explicit little-endian field reads/writes; do not serialize C/C++ structs directly.
- One canonical in-memory representation per message, shared by ASCII parsing and binary decoding.
- Strict bounds checks for all binary reads and variable-length records.
- Round-trip tests (`ASCII -> Binary -> ASCII` and `Binary -> ASCII -> Binary`) for each supported message.
- Windows / Visual Studio friendly build, with CMake for portability.

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

## Unicore N4 reference

The Unicore implementation is based on *Unicore Reference Commands Manual For N4 High Precision Products V2 CH R1.15* (2026-06). N4 binary records use the `AA 44 B5` synchronization bytes and a 24-byte binary header, followed by the message body and a 32-bit CRC.

See `docs/protocol_notes.md` for implementation-specific notes and unresolved protocol mappings.

## License

MIT.
