# Protocol notes

## Target architecture

The converter's binary target is NovAtel OEM7 format.

Important examples:

```text
NovAtel RANGEA -> NovAtel RANGEB
Unicore OBSVMA -> NovAtel RANGEB
Unicore GPSEPHA -> NovAtel GPSEPHEMB
Unicore BD3EPHA -> NovAtel BDSBCNAV1/2/3EPHEMERISB
```

Native Unicore binary output such as `OBSVMB` is not required.

## Unicore N4 R1.15 input

Primary implementation reference: *Unicore Reference Commands Manual For N4 High Precision Products_V2_CH_R1.15* (2026-06).

### ASCII framing

- ASCII records start with `#`.
- Header parsing follows Table 7-51.
- ASCII CRC covers the bytes after `#` and before `*`.
- Do not copy a source CRC into the target format; regenerate the NovAtel binary CRC from the target bytes.

### OBSVM source fields

- OBSVM Message ID is 12 in the native Unicore protocol. This source ID is not copied into RANGEB.
- Native OBSVM payload begins with `obs Number` (`ULONG`, 4 bytes).
- Each native binary observation occupies 40 bytes.
- Per-observation fields are `System Freq`, `PRN/slot`, pseudorange, ADR, pseudorange std x100, ADR std x10000, Doppler, C/N0 x100, reserved, lock time, and channel tracking status.
- The ASCII OBSVMA example prints the scaled integer quality fields directly.

## NovAtel OEM7 target

### Binary header

Use the standard 28-byte OEM7 binary header:

- sync `AA 44 12`
- header length: 28
- Message ID
- Message Type
- Port Address
- Message Length
- Sequence
- Idle Time
- Time Status
- GPS Week
- milliseconds into GPS week
- Receiver Status
- Reserved
- Receiver S/W Version

The standard NovAtel binary header does not contain a separate `TimeRef` byte.

### TimeRef / TimeStatus

For this project, Unicore `TimeRef` / `TimeStatus` follow the NovAtel conventions.

- Parse the source Unicore `TimeRef` and use it to interpret/validate the source week/ms time base.
- Do not add a non-standard `TimeRef` field to the NovAtel binary header.
- Write `TimeStatus` using the NovAtel one-byte GPS reference time-status enum.

Known NovAtel values include:

```text
20  UNKNOWN
60  APPROXIMATE
80  COARSEADJUSTING
100 COARSE
120 COARSESTEERING
130 FREEWHEELING
140 FINEADJUSTING
160 FINE
170 FINEBACKUPSTEERING
180 FINESTEERING
200 SATTIME
```

### RANGE target

- Message ID: 43.
- Payload starts with a 4-byte observation count.
- Each RANGE observation occupies 44 bytes.
- Per-observation fields are PRN/slot, `glofreq`, pseudorange, pseudorange sigma, ADR, ADR sigma, Doppler, C/N0, lock time, and channel tracking status.

## OBSVMA -> RANGEB mapping

Map source fields directly into the NovAtel RANGE representation; do not create an intermediate OBSVMB record.

Quality-field scaling:

```text
NovAtel psr sigma (m)      = Unicore psr_std_x100 / 100.0
NovAtel adr sigma (cycles) = Unicore adr_std_x10000 / 10000.0
NovAtel C/N0 (dB-Hz)       = Unicore cn0_x100 / 100.0
```

Other direct physical-value mappings:

```text
pseudorange -> pseudorange
ADR         -> ADR
Doppler     -> Doppler
lock time   -> lock time
```

PRN/frequency handling:

- For GLONASS, the Unicore `System Freq` field is already defined as frequency channel + 7 and is copied into NovAtel `glofreq`.
- For non-GLONASS signals, the source field is unused and is copied as supplied.

### Tracking-status passthrough rule

For this project, the 32-bit channel tracking status is **not translated or normalized**.

```text
RANGEA ch-tr-status  -> RANGEB tracking status: exact uint32 copy
OBSVMA ch-tr-status  -> RANGEB tracking status: exact uint32 copy
```

Requirements:

- Parse the ASCII hexadecimal value as an unsigned 32-bit integer.
- Write exactly the same 32-bit bit pattern into the target RANGE observation record.
- Do not modify system bits, signal-type bits, validity bits, reserved bits, channel number, or any other bit.
- Do not reconstruct the target word field-by-field.
- This is a project requirement even if vendor documentation describes individual bit meanings differently.
- Add tests using representative values such as `00181c23`, `00191c23`, and values with high bits set to prove bit-for-bit preservation.

## EPH / ION conversion matrix

The following Unicore N4 ASCII sources are mapped directly into NovAtel-framed binary records. Source field order and types come from the N4 R1.15 manual; target field order and types follow the corresponding NovAtel/compatible decoded-log schema.

| Unicore ASCII | NovAtel binary target | Message ID | Mapping notes |
| --- | --- | ---: | --- |
| `GPSEPHA` | `GPSEPHEMB` | 7 | 224-byte payload is field-compatible and copied field-by-field. |
| `GLOEPHA` | `GLOEPHEMERISB` | 723 | 144-byte payload is field-compatible. `Sloto` is already the offset PRN representation used by the target. |
| `QZSSEPHA` | `QZSSEPHEMERISB` | 1336 | Source PRN 1..10 becomes target PRN 193..202 (`+192`). Target-only fit/reserved bytes are zero. |
| `GALEPHA` | `GALEPHEMERISB` | 1122 | Legacy/compatibility decoded Galileo ephemeris layout; current OEM7 documentation replaces this log with separate FNAV/INAV logs. |
| `BDSEPHA` | `BD2EPHEMB` | 1047 | Compatibility layout. Source BDS PRN 1..63 becomes target offset PRN 161..223 (`+160`). |
| `GPSIONA` | `IONUTCB` | 8 | Eight ionospheric coefficients are copied. Source does not carry the UTC model contained in IONUTC, so unavailable UTC fields are zero. |
| `BDSIONA` | `BD2IONUTCB` | 2010 | Same rule as GPSION: ion coefficients copied; unavailable UTC half zeroed. |
| `GALIONA` | `GALIONOB` | 1127 | Ai0/Ai1/Ai2 and SF1..SF5 copied; source-only reserved word is dropped. |
| `IRNSSEPHA` | `NAVICEPHEMERISB` | 2123 | GPS week -> NavIC week by `-1024`; `A -> sqrt(A)` for RootA; URA variance -> NavIC URA index; alert/AutoNav bits extracted from source Flag. |
| `BD3EPHA` FreqType 0 | `BDSBCNAV1EPHEMERISB` | 2371 | GPS week -> BDT week by `-1356`; B1C/B2a TGD and B1C ISC mapped. |
| `BD3EPHA` FreqType 1 | `BDSBCNAV2EPHEMERISB` | 2372 | Same orbital/clock conversion; B2a ISC mapped. |
| `BD3EPHA` FreqType 2 | `BDSBCNAV3EPHEMERISB` | 2412 | B2b target; B2bI TGD mapped. |

### Deliberately unsupported source

`BD3IONA` is currently skipped silently. The N4 source is a 9-coefficient BeiDou-3 ionosphere model, and no verified equivalent NovAtel decoded log with the same model has been identified. The converter does not invent a target Message ID or silently reinterpret it as the 8-coefficient BDS/GPS Klobuchar-style model.

### Lossy/constructed fields

Some cross-vendor mappings cannot be fully lossless because the source and target messages are not identical:

- `GPSIONA`/`BDSIONA` do not contain the UTC polynomial/leap-second fields required by `IONUTC`/`BD2IONUTC`; these fields are written as zero rather than populated with unrelated source metadata.
- `QZSSEPHA` does not contain the four target bytes following the common 224-byte ephemeris block; they are written as zero.
- `BD3EPHA` does not provide every integrity/reserved bit in the target B-CNAV status fields. Verified source health and SISMAI bits are populated; unavailable target bits are zero.
- `IRNSSEPHA` provides semi-major axis `A` and URA variance, while `NAVICEPHEMERIS` expects `RootA` and a URA index. These are converted mathematically rather than copied byte-for-byte.

## Native NovAtel EPH / ION codecs

The converter also supports ASCII <-> binary for these target records so generated binary data can be inspected and round-tripped:

- `GPSEPHEM` (7)
- `GLOEPHEMERIS` (723)
- `QZSSEPHEMERIS` (1336)
- `GALEPHEMERIS` compatibility log (1122)
- `BD2EPHEM` compatibility log (1047)
- `IONUTC` (8)
- `BD2IONUTC` compatibility log (2010)
- `GALIONO` (1127)
- `NAVICEPHEMERIS` (2123)
- `BDSBCNAV1EPHEMERIS` (2371)
- `BDSBCNAV2EPHEMERIS` (2372)
- `BDSBCNAV3EPHEMERIS` (2412)

## Unsupported records in mixed files

Unsupported ASCII records are skipped silently. Supported message names with malformed fields remain errors. For binary input, a valid but unsupported NovAtel record is also skipped silently after its framing/CRC is validated, allowing mixed binary logs to be converted without aborting on unrelated messages.

## CRC

Unicore ASCII CRC is only for validating the source line. NovAtel binary output must use the NovAtel CRC over the generated binary header + body.
