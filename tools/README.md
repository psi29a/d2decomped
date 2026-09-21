# tools/

Scripts, converters, and Ghidra helpers used during decompilation.

## Planned

- `ghidra/` — headless scripts (`.java`), Data Type archives (`.gdt`),
  postprocess helpers. Ghidra project files themselves stay outside git.
- `mpq-dump/` — CLI: extract or list an MPQ. Thin wrapper over StormLib.
- `dcc-dump/`, `dc6-dump/` — decode sprite frames to PNG for eyeballing.
- `txt-diff/` — normalize Excel-style .txt files for readable diffs.

Anything longer than ~100 lines gets its own subdirectory with a
README naming inputs, outputs, and how to run it.
