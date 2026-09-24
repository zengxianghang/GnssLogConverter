# Tracking-status passthrough requirement

For both supported RANGE input paths, the 32-bit tracking-status value is preserved exactly:

```text
RANGEA -> RANGEB
OBSVMA -> RANGEB
```

Rules:

1. Parse the ASCII `ch-tr-status` hexadecimal field as an unsigned 32-bit integer.
2. Write the same 32-bit value into the target RANGEB observation record.
3. Do not modify or reconstruct any bit.
4. Do not translate system, signal-type, validity, channel-number, or reserved bits.
5. This passthrough requirement takes precedence over any cross-vendor semantic differences in bit definitions.
6. Unit tests must include representative low/high-bit patterns and assert exact equality.
