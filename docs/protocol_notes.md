# Protocol notes

## Unicore N4 R1.15

Primary implementation reference: *Unicore Reference Commands Manual For N4 High Precision Products_V2_CH_R1.15* (2026-06).

### Binary framing

- Sync: `AA 44 B5`.
- Binary header: 24 bytes.
- Header fields at documented offsets:
  - CPU idle: byte 3
  - Message ID: bytes 4-5
  - Message length: bytes 6-7
  - TimeRef: byte 8
  - TimeStatus: byte 9
  - week: bytes 10-11
  - milliseconds: bytes 12-15
  - version: bytes 16-19
  - reserved: byte 20
  - leap seconds: byte 21
  - output delay: bytes 22-23
- CRC is 32-bit and covers the binary header plus payload.
- Appendix 1 initializes CRC to zero and applies the reflected `0xEDB88320` polynomial/table with no final XOR in the supplied C example.

### ASCII framing

- ASCII log records start with `#`.
- ASCII CRC covers the bytes after `#` and before `*`.
- Do not copy a source CRC into the target format; regenerate it from target bytes/text.

### OBSVM

- Message ID: 12.
- Payload starts with `obs Number` (`ULONG`, 4 bytes).
- Each observation occupies 40 bytes.
- Per-observation fields are `System Freq`, `PRN/slot`, pseudorange, ADR, pseudorange std x100, ADR std x10000, Doppler, C/N0 x100, reserved, lock time, and channel tracking status.
- Important: the R1.15 ASCII OBSVMA example also prints the scaled integer fields (for example pseudorange std and C/N0) as their scaled integer values. Do not divide these values when serializing OBSVMA unless later authoritative documentation proves otherwise.

### Open item: TimeRef / TimeStatus numeric values

R1.15 identifies the one-byte binary fields and shows ASCII strings such as `GPS,FINE`, but the reviewed section does not define their numeric binary enum mapping. Do not infer the numeric mapping from names alone. Obtain an authoritative mapping from official documentation or a known-good receiver binary sample before enabling full ASCII -> binary header conversion.
