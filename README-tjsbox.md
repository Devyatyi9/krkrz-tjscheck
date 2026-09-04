# tjsbox - Standalone TJS2 Sandbox

Runs a TJS2 script outside the game and reports what happened.

`tjscheck` answers "will this parse". That leaves the mistakes that cost the
most: a call on a member that turned out to be `void`, a name resolving against
the wrong `this`. TJS2 is dynamically typed, so no amount of parsing finds those
— but running the code does. In the game the same mistakes are worse than
useless to debug: an exception thrown inside the engine's own dispatch takes the
process down with nothing in any log, and the signature only shows up in a crash
dump (`E06D7363.?AVeTJSError@TJS@@`). Here it is one line with a line number.

## Usage

```
tjsbox <file.tjs>
tjsbox --stdin
tjsbox --disasm <file.tjs>
tjsbox --advise <file.tjs>
tjsbox --help
```

- **Exit 0** — the script ran to the end. Its value, if any, goes to stdout.
- **Exit 1** — the script failed to compile or to run; the error goes to stderr
  with a line number and, for runtime failures, a trace.
- **Exit 2** — the tool could not read its input.

Encodings match the checker: UTF-16 LE with BOM, UTF-8 with or without one. A
file that passes `tjscheck` can be handed to `tjsbox` unchanged.

## Disassembly

`--disasm` compiles the script and prints the bytecode the generator emits,
without running it. It exists to answer what a construct actually compiles to,
for the times the language reference is silent — where `incontextof` binds, what
a class member does at construction, why `typeof x.y` does not fail on a missing
member.

```
> tjsbox --disasm guard.tjs
(function expression) (anonymous) 0x00FA5A78
00000000 typeofd %1, %-1.*0      // *0 = (string)"orig"
00000004 const   %2, *1          // *1 = (string)"Object"
00000007 ceq     %1, %2
00000010 jnf     000000021
00000012 calld   %1, %-1.*0(%-3) // *0 = (string)"orig"
```

Member names survive code generation, so the listing is readable rather than
numeric.

It is not a checker, and is not on the way to becoming one. The listing shows
the guard, but nothing in it says which functions the engine will call, and that
is the half that decides whether a missing guard matters. Testing against stubs
answers the same question directly and without heuristics.

## Advice

`--advise` runs the script and then reports one thing:

```
advice: hook.tjs installed as 'onTag' calls '__orig' without checking it is
        still there
        guard it: if (typeof x.__orig == "Object") x.__orig(...)
```

That is the shape that took the game down twice. A wrapper installed on an
engine object cannot be taken off one that a later scene replaced, so it
outlives its own setup and finds the member it saved gone; on the engine's
dispatch path the resulting exception is caught by nothing and the process
disappears without a line in any log.

The rule is read off the bytecode: an assignment names its member in the line's
own comment, a function value carries the address its context header repeats,
and a `typeof` guard compiles to a `typeofd` naming what it tested. Three
narrowings keep it quiet — only functions actually installed on something, only
names the script itself stored (so ordinary calls are not candidates), and only
calls outside a `try` region, which the `entry`/`extry` pair marks.

Advice never changes the exit code, and is skipped entirely when the script
failed, so it cannot bury the error that matters.

**One rule, deliberately.** Every rule here must point at a failure that was
actually measured in the game, not at something that looks wrong. A rule written
from a plausible-sounding hunch produces confident, repeated, wrong advice — the
sandbox stubs are dictionaries, for instance, and a rule about unqualified
global names would fire on code the game runs perfectly well.

## Reporting a value

Only `return` at top level reports a value. A trailing expression does not:

```tjs
var a = 2 + 3;
"result is " + a;         // nothing reaches stdout
return "result is " + a;  // "result is 5"
```

This is also why a script that ends in `return` cannot simply have checks
appended to it — the `return` ends the script. Wrap it to test around it:

```tjs
(function(){
    // the script under test, verbatim
})();
// checks here
```

## What is present

A bare `tTJS` registers `Array`, `Dictionary`, `Date`, `Math`,
`RandomGenerator`, `Exception` and `RegExp`. That is all.

There is no `kag`, no layers, no sound buffers — and no `System`, `Debug` or
`Scripts` either: those live in KiriKiri's own layer (`base/ScriptMgnIntf.cpp`),
which this project does not compile. `Scripts.getObjectKeys`, widely used in
game scripts, is not even there — it comes from the `scriptsEx` plugin
(wamsoft/scriptsEx), which is outside the engine entirely.

**This is deliberate, not an omission.** Write the objects under test as TJS
dictionaries in the script itself:

```tjs
global.kag = %[];
kag.conductor = %[];
kag.conductor.onTag = function(tag) { return "orig:" + tag; };
```

Stubs are the point rather than a workaround: a stub can be made deliberately
wrong — a saved handler left `void`, a member missing — which is exactly the
state that is hard to reach on purpose in a running game. Linking the engine
layer would give objects from a source tree that is not the one the game runs,
so it would look more faithful while being less true. If a test ever genuinely
needs the real thing, add `base/` to the project then, for that reason.

## Building

- **Project:** `vcproj/tjsbox.vcxproj` (Visual Studio, Windows x86)
- **Configurations:** Debug → `tjsbox_d.exe`, Release → `tjsbox.exe`
- Shares every source with the checker; only `sandbox.cpp` differs from
  `main.cpp`. Nothing is duplicated.

## Tests

```powershell
.\tests\Test-TjsboxCli.ps1 -Sandbox .\tjsbox.exe
```

Covers what the sandbox exists for: a value reported through `return`, a
trailing expression reporting nothing, a call on a `void` member failing with a
line number, the guarded form surviving, an exception carrying its message, a
syntax error still stopping before anything runs, `--disasm` listing a script
that would have failed had it run, and `--advise` firing on the unguarded
call-through, staying quiet on the guarded one and on a call inside `try`, and
leaving a real error unburied.
