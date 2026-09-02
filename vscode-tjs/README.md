# TJS2 for VSCode

Syntax highlighting for `.tjs` files, and syntax errors from `tjscheck`
underlined where they are.

## Installing

The extension is not published; link it from this repository:

```powershell
New-Item -ItemType SymbolicLink -Path "$env:USERPROFILE\.vscode\extensions\tjs-syntax" -Target "<path to this repository>\vscode-tjs"
```

A symbolic link needs administrator rights or developer mode. Without either,
copy the folder instead — edits here will then not reach the installed copy.

Restart VSCode afterwards.

## Settings

`tjs.checkerPath` — where `tjscheck.exe` is. Left empty it looks beside the
extension first, where a shipped copy belongs, then one level up for a build
of this repository, and finally on `PATH`. Nothing is taken from the open
workspace: that would pick up an unrelated file of the same name.

If it is found nowhere, the extension says so once rather than reporting a
clean file.

`tjs.debounceMs` — how long to wait after typing stops, 300 ms by default. A run
costs about 7 ms, so a shorter wait is affordable.

## What it reports

The checker stops at the first error, so a file never carries more than one
problem at a time. The position points at the token the parser choked on, which
is usually the mistake itself; when the file simply ends early the position
lands on the last line, which is where parsing stopped.

## About the highlighting

The grammar names scopes rather than colours, so the editor's own theme decides
how each one looks. Control flow, declarations, primitive types, language
constants, language variables and word operators are separate scopes, so they
come out looking like the same roles do in other languages.

The reserved words come from the TJS2 lexer itself (`tjs2/tjsLex.cpp`) rather
than from a JavaScript grammar by analogy. TJS2 has `incontextof`, `isvalid`,
`invalidate`, `octet`, `real`, `property`, `getter`, `setter`, `global` and
`synchronized`, none of which a JavaScript grammar knows. It also has forms a
JavaScript grammar would colour wrongly: `%[ ... ]` dictionaries, `<% ... %>`
octet literals, and `@"text ${expression}"` strings whose embedded expressions
are highlighted as code.
