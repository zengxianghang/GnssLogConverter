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

## Status

Repository bootstrap in progress. The first implementation milestone is the common byte/CRC layer plus Unicore N4 header and `OBSVM` round-trip conversion, followed by EPH/ION records and NovAtel OEM7 support.

## Design principles

- C-style C++ with no third-party runtime dependencies.
- Explicit little-endian field reads/writes; do not serialize C/C++ structs directly.
- One canonical in-memory representation per message, shared by ASCII parsing and binary decoding.
- Strict bounds checks for all binary reads and variable-length records.
- Round-trip tests (`ASCII -> Binary -> ASCII` and `Binary -> ASCII -> Binary`) for each supported message.
- Windows / Visual Studio friendly build, with CMake for portability.

## Planned CLI

```powershell
GnssLogConverter.exe input.log output.bin
GnssLogConverter.exe input.bin output.log

GnssLogConverter.exe input.log output.bin --to binary
GnssLogConverter.exe input.bin output.log --to ascii

GnssLogConverter.exe input.log output.bin --vendor novatel
GnssLogConverter.exe input.log output.bin --vendor unicore
```

When the direction is not specified, the tool will infer it from the input framing and/or filename extension when unambiguous.

## Unicore N4 reference

The Unicore implementation is based on *Unicore Reference Commands Manual For N4 High Precision Products V2 CH R1.15* (2026-06). In particular, N4 binary records use the `AA 44 B5` synchronization bytes and a 24-byte binary header, followed by the message body and a 32-bit CRC.

## License

MIT (to be added during repository bootstrap).
