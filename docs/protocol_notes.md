# Protocol notes

## Target architecture

The converter's binary target is NovAtel OEM7 format.

Important examples:

```text
NovAtel RANGEA -> NovAtel RANGEB
Unicore OBSVMA -> NovAtel RANGEB
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

PRN/frequency and tracking-status conversion require protocol-aware translation:

- For GLONASS, convert the Unicore system-frequency field into NovAtel `glofreq` (`frequency channel + 7`) as defined by the target RANGE format.
- For non-GLONASS signals, `glofreq` is normally zero except target-specific cases such as QZSS L1C/B.
- Do not blindly copy the Unicore 32-bit tracking-status word. Several bit positions have similar meanings, but signal-type values and some status bits differ between the documented Unicore and current NovAtel definitions. Build the target status word field-by-field.

## CRC

Unicore ASCII CRC is only for validating the source line. NovAtel binary output must use the NovAtel CRC over the generated binary header + body.
