# AGENTS.md

## Project goal

Implement reliable conversion into NovAtel OEM7 binary records from supported NovAtel and Unicore ASCII logs, plus the reverse NovAtel binary-to-ASCII path.

Important cross-vendor example:

```text
Unicore OBSVMA -> NovAtel RANGEB
```

Do not implement an intermediate `OBSVMB` output path unless a later requirement explicitly asks for native Unicore binary.

## Coding rules

- Prefer C-style C++: small functions, plain structs, fixed buffers/arrays, and explicit ownership.
- Avoid STL containers in protocol hot paths unless there is a clear safety/maintainability benefit.
- Declare variables close to first use.
- Do not serialize/deserialise protocol structs with `memcpy`, `fread`, `fwrite`, or compiler packing. Read/write each protocol field explicitly at its documented byte offset.
- Treat all external lengths/counts as untrusted. Check overflow and buffer bounds before pointer arithmetic or allocation.
- Correctness and protocol fidelity take priority over speed. Optimize only after tests exist.
- Keep vendor-specific parsing under `src/novatel/` and `src/unicore/`; cross-vendor mappings should be explicit and isolated.
- Map differing field units/scaling deliberately.
- Project-specific exception: for `RANGEA -> RANGEB` and `OBSVMA -> RANGEB`, copy the 32-bit `ch-tr-status` / tracking-status word bit-for-bit. Do not translate, normalize, reinterpret, or rebuild any bit, including system/signal/validity/reserved bits.
- Unicore `TimeRef` and `TimeStatus` follow the NovAtel conventions for this project. When producing standard NovAtel binary, write the NovAtel `TimeStatus` enum; do not invent a separate `TimeRef` field in the NovAtel header.

## Required verification

Before opening or updating a PR:

1. Build the project.
2. Run all tests.
3. Add tests for every new message or protocol field mapping.
4. For `RANGEA -> RANGEB` and `OBSVMA -> RANGEB`, include tests proving exact 32-bit tracking-status preservation.
5. For cross-vendor conversion, verify units/scaling independently, except tracking status which follows the explicit passthrough rule above.
6. For native NovAtel ASCII/binary codecs, add round-trip coverage where practical.
7. Record implementation notes in the PR body, including protocol source, assumptions, and any unresolved ambiguity.
