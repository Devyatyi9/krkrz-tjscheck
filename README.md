# tjscheck - Standalone TJS2 Syntax Checker

A minimal command-line TJS2 syntax validator extracted from [KiriKiri-Z](https://github.com/krkrz/krkrz).

Checks TJS2 script files for syntax errors without executing them.

## Usage

```
tjscheck <file.tjs>
tjscheck --expression "<code>"
tjscheck --stdin
```

- `tjscheck -e "<code>"` is the short form of `--expression`; it checks one
  TJS expression without executing it.
- `tjscheck -s` is the short form of `--stdin`; it reads a standalone TJS script
  from standard input.
- `tjscheck --help` shows the complete usage.
- **Exit 0, stdout `OK`** - valid TJS2 syntax.
- **Exit 1, stderr diagnostic** - invalid TJS syntax.
- **Exit 2, stderr diagnostic** - invalid CLI usage, input, encoding, or checker
  error.
- Options are mutually exclusive. Use `--` before a file name that starts with
  `-`.

## Features

- Supports UTF-16 LE (BOM), UTF-8 with BOM, and plain UTF-8 encoding (auto-detected)
- No KiriKiri runtime required — pure syntax analysis
- English error messages (based on tjsError_jp.h from tjsdisasm)
- Built with `TJS_NO_REGEXP` (Oniguruma excluded). Regexp literals can still be
  syntactically accepted, but this checker does not validate RegExp runtime
  availability.
- ~1.3 MB binary (Release, static link)

## Limitations

- **ATRI-style fragments** — `function(x){...}` and `%[...]` at top level without `;` are valid in-game (via eval/module) but rejected by standalone parsing. These are not bugs — they require the game's runtime context.
- **No execution** — `Parse()` only, no bytecode interpretation
- **No TJS-level tag hooks** — KAG tag interception (`kag.addHook()`, `kag.conductor.onTag`) is a separate concern
- **No KAG validation** — `.ks` scenario files are not checked
- **Compiled bytecode** (`TJS2` magic signature) is auto-detected and reported as `OK (compiled bytecode)`
- **No runtime compatibility check** - successful syntax checking does not prove
  that all classes, native functions, or RegExp are available at runtime

## Building

- **Solution:** `vcproj/tjscheck.vcxproj` (Visual Studio, Windows x86)
- **Configurations:** Debug → `tjscheck_d.exe`, Release → `tjscheck.exe`
- **Defines:** `_UNICODE`, `UNICODE`, `TJS_NO_REGEXP`, `_CRT_SECURE_NO_WARNINGS`

## Testing

Build a configuration, then run the process-level CLI checks:

```powershell
msbuild ".\vcproj\tjscheck.vcxproj" /m /p:Configuration=Release /p:Platform=Win32
.\tests\Test-TjscheckCli.ps1 .\tjscheck.exe
```

## Source

Derived from `KiriKiri-Z`. The `tjs2/` directory is the core TJS2 engine; `main.cpp` is the standalone checker entry point.

## References

- `tjs2/tjsErrorInc.h` — English error messages (translated from `tjsError_jp.h` in tjsdisasm)
