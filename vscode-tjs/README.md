# TJS syntax check for VSCode

Runs `tjscheck` over the `.tjs` file being edited and underlines the syntax
error where it is.

Highlighting is not part of this. Install
[TJS](https://marketplace.visualstudio.com/items?itemName=Biscrat.tjs-vscode)
for that — it registers the `tjs` language this extension attaches to, and adds
snippets, folding and ctags besides. What it does not do is check the file, and
that is the whole of what this one adds.

## Installing

Not published. Link it from this repository:

```powershell
New-Item -ItemType SymbolicLink -Path "$env:USERPROFILE\.vscode\extensions\tjs-syntax-check" -Target "<path to this repository>\vscode-tjs"
```

A symbolic link needs administrator rights or developer mode. Without either,
copy the folder — edits here will then not reach the installed copy.

For a copy that installs and uninstalls like any other extension, package it
instead:

```powershell
npx @vscode/vsce package
```

and install the resulting `.vsix` from the Extensions view. A linked extension
does not appear under the Marketplace tab; look for it under `@installed`.

## Settings

`tjscheck.checkerPath` — where `tjscheck.exe` is. Left empty it looks beside the
extension first, where a shipped copy belongs, then one level up for a build of
this repository, and finally on `PATH`. Nothing is taken from the open
workspace: that would pick up an unrelated file of the same name.

`tjscheck.debounceMs` — how long to wait after typing stops, 300 ms by default.
A run costs about 7 ms, so a shorter wait is affordable.

If the checker is found nowhere, the extension says so once rather than
reporting a clean file.

## What it reports

The checker stops at the first error, so a file never carries more than one
problem at a time. The position points at the token the parser choked on, which
is usually the mistake itself; when the file simply ends early the position
lands on the last line, which is where parsing stopped.
