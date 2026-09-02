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

and install the resulting `.vsix` from the Extensions view. Copy `tjscheck.exe`
into this folder first and it travels with the package; it is ignored by git,
so it will not be committed. A linked extension
does not appear under the Marketplace tab; look for it under `@installed`.

## Settings

`tjscheck.checkerPath` — where `tjscheck.exe` is. A packaged build carries the
checker inside it, so this normally needs no value. Left empty it looks beside
the extension first, then one level up for a build of this repository, and
finally on `PATH`. Nothing is taken from the open workspace: that would pick up
an unrelated file of the same name.

`tjscheck.debounceMs` — how long to wait after typing stops, 300 ms by default.
A run takes roughly 100 ms through this path — the checker itself needs about
7 ms, the rest is starting a process and handing it the buffer — so a shorter
wait mostly means more processes rather than faster answers.

If the checker is found nowhere, the extension says so once rather than
reporting a clean file.

## Where to look

Errors land in the Problems panel and under the text itself, not in Output.

The **TJS syntax check** output channel is for the extension rather than the
file: which checker it settled on, and a line per run with the result and how
long it took. That is what separates "this file is fine" from "nothing ran".

## What it reports

The checker stops at the first error, so a file never carries more than one
problem at a time. The position points at the token the parser choked on, which
is usually the mistake itself; when the file simply ends early the position
lands on the last line, which is where parsing stopped.
