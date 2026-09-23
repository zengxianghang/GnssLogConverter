# AGENTS.md

## Project goal

Implement reliable bidirectional conversion between native ASCII and binary logs for NovAtel OEM7 and Unicore N4.

## Coding rules

- Prefer C-style C++: small functions, plain structs, fixed buffers/arrays, and explicit ownership.
- Avoid STL containers in protocol hot paths unless there is a clear safety/maintainability benefit.
- Declare variables close to first use.
- Do not serialize/deserialise protocol structs with `memcpy`, `fread`, `fwrite`, or compiler packing. Read/write each protocol field explicitly at its documented byte offset.
- Treat all external lengths/counts as untrusted. Check overflow and buffer bounds before pointer arithmetic or allocation.
- Correctness and protocol fidelity take priority over speed. Optimize only after tests exist.
- Keep vendor-specific logic under `src/novatel/` and `src/unicore/`; common byte/CRC/file utilities belong in `src/`.
- Do not guess undocumented enum values. Isolate unresolved mappings and fail clearly until an authoritative mapping is available.

## Required verification

Before opening or updating a PR:

1. Build the project.
2. Run all tests.
3. Add tests for every new message or protocol field mapping.
4. For codecs, add round-trip coverage where practical.
5. Record implementation notes in the PR body, including protocol source, assumptions, and any unresolved ambiguity.
