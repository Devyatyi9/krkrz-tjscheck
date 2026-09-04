param(
    [Parameter(Mandatory = $true)]
    [string]$Sandbox
)

# Checks that tjsbox runs scripts rather than only parsing them, and that the
# errors it reports are the ones worth having: the runtime failures a syntax
# check cannot see.

$ErrorActionPreference = 'Stop'
$Sandbox = (Resolve-Path -LiteralPath $Sandbox).Path
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

$script:Failures = 0

function Invoke-Sandbox {
    param([string]$Source, [string[]]$Switches = @())

    $file = [System.IO.Path]::GetTempFileName()
    $file = [System.IO.Path]::ChangeExtension($file, '.tjs')
    [System.IO.File]::WriteAllText($file, $Source, $Utf8NoBom)
    try {
        $out = & $Sandbox @Switches $file 2>&1 | ForEach-Object { "$_" }
        [pscustomobject]@{
            ExitCode = $LASTEXITCODE
            Output   = ($out -join "`n")
        }
    } finally {
        Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
    }
}

function Assert-Case {
    param(
        [string]$Name,
        [string]$Source,
        [int]$ExpectedExit,
        [string]$ExpectedMatch,
        [string[]]$Switches = @()
    )

    $result = Invoke-Sandbox -Source $Source -Switches $Switches
    if ($result.ExitCode -ne $ExpectedExit) {
        Write-Host "FAIL $Name : exit $($result.ExitCode), expected $ExpectedExit"
        Write-Host "     output: $($result.Output)"
        $script:Failures++
        return
    }
    if ($ExpectedMatch -and $result.Output -notmatch $ExpectedMatch) {
        Write-Host "FAIL $Name : output did not match /$ExpectedMatch/"
        Write-Host "     output: $($result.Output)"
        $script:Failures++
        return
    }
    Write-Host "ok   $Name"
}

Assert-Case -Name 'runs and returns a value' -ExpectedExit 0 -ExpectedMatch 'result is 5' -Source @'
var a = 2 + 3;
return "result is " + a;
'@

# A trailing expression is not the script's value; only return reports one.
Assert-Case -Name 'trailing expression is not a value' -ExpectedExit 0 -ExpectedMatch '^$' -Source @'
var a = 2 + 3;
"result is " + a;
'@

# The failure this tool exists for: valid syntax, fatal at runtime. In the game
# the same mistake takes the process down without printing anything.
Assert-Case -Name 'calling a member that is void fails with a line' -ExpectedExit 1 -ExpectedMatch 'Not a function|\(4\)' -Source @'
global.obj = %[];
obj.saved = void;
obj.call = function() {
    return this.saved();
};
obj.call();
'@

Assert-Case -Name 'the guarded form survives' -ExpectedExit 0 -ExpectedMatch 'survived' -Source @'
global.obj = %[];
obj.saved = void;
obj.call = function() {
    if (typeof this.saved == "Object") {
        return this.saved();
    }
    return "survived";
};
return obj.call();
'@

Assert-Case -Name 'exceptions carry their message' -ExpectedExit 1 -ExpectedMatch 'deliberate' -Source @'
throw new Exception("deliberate");
'@

# Syntax errors still stop before anything runs.
Assert-Case -Name 'syntax error is reported' -ExpectedExit 1 -ExpectedMatch 'syntax|error' -Source @'
var a = ;
'@

# --disasm compiles without running, so a script that would fail at runtime
# still lists. The check looks for the opcode the guard compiles to, which is
# what makes the listing worth reading at all.
Assert-Case -Name 'disasm lists without running' -Switches '--disasm' -ExpectedExit 0 -ExpectedMatch 'typeofd.*"orig"' -Source @'
var o = %[];
o.f = function() {
	if (typeof this.orig == "Object") return this.orig();
};
o.f();
'@

# --advise: the rule fires on the shape that actually took the game down.
Assert-Case -Name 'advise catches an unguarded call-through' -Switches '--advise' -ExpectedExit 0 -ExpectedMatch "calls '__orig' without checking" -Source @'
var kag = %[ conductor: %[ onTag: function(t) { return "orig:" + t; } ] ];
kag.conductor.__orig = kag.conductor.onTag;
kag.conductor.onTag = function(tag) { return this.__orig(tag); };
return kag.conductor.onTag("msg");
'@

# ...and stays quiet once the guard is there, or the advice is noise.
Assert-Case -Name 'advise is quiet on the guarded form' -Switches '--advise' -ExpectedExit 0 -ExpectedMatch 'nothing to report' -Source @'
var kag = %[ conductor: %[ onTag: function(t) { return "orig:" + t; } ] ];
kag.conductor.__orig = kag.conductor.onTag;
kag.conductor.onTag = function(tag) {
	var r = void;
	if (typeof this.__orig == "Object") r = this.__orig(tag);
	return r;
};
return kag.conductor.onTag("msg");
'@

# A call inside try/catch is caught, so it is not the failure being looked for.
Assert-Case -Name 'advise ignores a call inside try' -Switches '--advise' -ExpectedExit 0 -ExpectedMatch 'nothing to report' -Source @'
var o = %[];
o.saved = function() { return 1; };
o.f = function() { try { return this.saved(); } catch (e) {} };
return o.f();
'@

# Advice is advice: a clean run stays exit 0 even when something is reported,
# and a script that failed reports the failure instead of being lectured.
Assert-Case -Name 'advise does not bury a real error' -Switches '--advise' -ExpectedExit 1 -ExpectedMatch 'missing' -Source @'
var o = %[];
return o.missing();
'@

if ($script:Failures -gt 0) {
    Write-Host ""
    Write-Host "$($script:Failures) test(s) failed."
    exit 1
}

Write-Host ""
Write-Host "All sandbox tests passed."
# Explicit: without it the script inherits $LASTEXITCODE from the last sandbox
# run, and the suite reports failure after passing every case.
exit 0
