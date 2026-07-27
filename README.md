# tjscheck — Standalone TJS2 Syntax Checker

A minimal command-line TJS2 syntax validator extracted from [KiriKiri-Z](https://github.com/krkrz/krkrz).

Checks TJS2 script files for syntax errors without executing them.

## Usage

```
tjscheck <file.tjs>
```

- **Exit 0, stdout `OK`** — valid TJS2 syntax (for standalone scripts)
- **Exit 1, stderr `Syntax error at line N`** — invalid syntax

## Features

- Supports UTF-16 LE (BOM), UTF-8 with BOM, and plain UTF-8 encoding (auto-detected)
- No KiriKiri runtime required — pure syntax analysis
- English error messages (based on tjsError_jp.h from tjsdisasm)
- Built with `TJS_NO_REGEXP` (Oniguruma excluded) — no impact for scripts that don't use Regexp
- ~1.3 MB binary (Release, static link)

## Limitations

- **ATRI-style fragments** — `function(x){...}` and `%[...]` at top level without `;` are valid in-game (via eval/module) but rejected by standalone parsing. These are not bugs — they require the game's runtime context.
- **No execution** — `Parse()` only, no bytecode interpretation
- **No TJS-level tag hooks** — KAG tag interception (`kag.addHook()`, `kag.conductor.onTag`) is a separate concern
- **No KAG validation** — `.ks` scenario files are not checked
- **Compiled bytecode** (`TJS2` magic signature) is auto-detected and reported as `OK (compiled bytecode)`

## Building

- **Solution:** `vcproj/tjscheck.vcxproj` (Visual Studio, Windows x86)
- **Configurations:** Debug → `tjscheck_d.exe`, Release → `tjscheck.exe`
- **Defines:** `_UNICODE`, `UNICODE`, `TJS_NO_REGEXP`, `_CRT_SECURE_NO_WARNINGS`

## Source

Derived from `KiriKiri-Z`. The `tjs2/` directory is the core TJS2 engine; `main.cpp` is the standalone checker entry point.

## References

- `tjs2/tjsErrorInc.h` — English error messages (translated from `tjsError_jp.h` in tjsdisasm)
