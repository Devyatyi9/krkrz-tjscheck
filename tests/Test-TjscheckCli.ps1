param(
    [Parameter(Mandatory = $true)]
    [string]$Checker
)

$ErrorActionPreference = 'Stop'
$Checker = (Resolve-Path -LiteralPath $Checker).Path
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$Utf16LeBom = [System.Text.UnicodeEncoding]::new($false, $true)

function Invoke-Checker {
    param(
        [string[]]$Arguments,
        [byte[]]$InputBytes,
        [string]$WorkingDirectory
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Checker
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = $null -ne $InputBytes
    if ($null -ne $WorkingDirectory) {
        $startInfo.WorkingDirectory = $WorkingDirectory
    }
    foreach ($argument in $Arguments) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    if ($null -ne $InputBytes) {
        $process.StandardInput.BaseStream.Write($InputBytes, 0, $InputBytes.Length)
        $process.StandardInput.Close()
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdout; Stderr = $stderr }
}

function Assert-Result {
    param(
        [string]$Name,
        $Actual,
        [int]$ExitCode,
        [string]$StdoutPattern,
        [string]$StderrPattern
    )

    if ($Actual.ExitCode -ne $ExitCode -or
        $Actual.Stdout -notmatch $StdoutPattern -or
        $Actual.Stderr -notmatch $StderrPattern) {
        throw "$Name failed: exit=$($Actual.ExitCode), stdout=[$($Actual.Stdout)], stderr=[$($Actual.Stderr)]"
    }
}

$noStderr = '^$'
Assert-Result 'help' (Invoke-Checker @('--help')) 0 '--expression' $noStderr
Assert-Result 'valid expression' (Invoke-Checker @('-e', '1 + 2 * 3')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'Unicode expression' (Invoke-Checker @('-e', '"日本語" + "тест"')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'invalid expression' (Invoke-Checker @('--expression', '1 +')) 1 '^$' '<expression>:1:'
Assert-Result 'regexp expression' (Invoke-Checker @('-e', '/abc/i')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'valid regexp pattern' (Invoke-Checker @('-r', '^[A-Z]+$', 'i')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'empty regexp pattern' (Invoke-Checker @('--regexp', '')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'invalid regexp pattern' (Invoke-Checker @('--regexp', '[')) 1 '^$' '<regexp>: invalid pattern:'
Assert-Result 'invalid regexp flags' (Invoke-Checker @('-r', 'abc', 'z')) 2 '^$' '<regexp>: invalid flags: z'
Assert-Result 'ATRI line ending regexp' (Invoke-Checker @('-r', '[\r]?\n', 'g')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'ATRI filename regexp' (Invoke-Checker @('-r', '[\:\?\*\<\>\|]')) 0 '^OK\r?\n$' $noStderr
Assert-Result 'ATRI invalid Unicode regexp' (Invoke-Checker @('-r', '@([a-zA-Z0-9_\x0100-\xFFFF]*)(\.[a-zA-Z0-9_]+)?')) 1 '^$' 'too short multibyte code string'
Assert-Result 'usage error' (Invoke-Checker @('--stdin', '-e', '1')) 2 '^$' 'Usage:'

$validScript = $Utf8NoBom.GetBytes("var x = 1;`n")
$invalidScript = $Utf8NoBom.GetBytes("var x = ;`n")
$utf16Script = $Utf16LeBom.GetPreamble() + $Utf16LeBom.GetBytes("var x = 1;`n")
$invalidUtf8 = [byte[]](0xC3, 0x28)
$invalidUtf16 = [byte[]](0xFF, 0xFE, 0x61)
Assert-Result 'valid stdin' (Invoke-Checker @('-s') $validScript) 0 '^OK\r?\n$' $noStderr
Assert-Result 'invalid stdin' (Invoke-Checker @('--stdin') $invalidScript) 1 '^$' '<stdin>:'
Assert-Result 'UTF-16 stdin' (Invoke-Checker @('--stdin') $utf16Script) 0 '^OK\r?\n$' $noStderr
Assert-Result 'invalid UTF-8 stdin' (Invoke-Checker @('--stdin') $invalidUtf8) 2 '^$' 'not valid UTF-8 or UTF-16 LE text'
Assert-Result 'invalid UTF-16 stdin' (Invoke-Checker @('--stdin') $invalidUtf16) 2 '^$' 'not valid UTF-8 or UTF-16 LE text'

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "tjscheck-$PID"
$temporaryFile = Join-Path $temporaryDirectory 'input.tjs'
$optionFile = Join-Path $temporaryDirectory '-input.tjs'
try {
    [System.IO.Directory]::CreateDirectory($temporaryDirectory) | Out-Null
    [System.IO.File]::WriteAllBytes($temporaryFile, $validScript)
    Assert-Result 'valid file' (Invoke-Checker @($temporaryFile)) 0 '^OK\r?\n$' $noStderr

    [System.IO.File]::WriteAllBytes($temporaryFile, $invalidScript)
    Assert-Result 'invalid file' (Invoke-Checker @($temporaryFile)) 1 '^$' ':1:'

    [System.IO.File]::WriteAllBytes($temporaryFile, $utf16Script)
    Assert-Result 'UTF-16 file' (Invoke-Checker @($temporaryFile)) 0 '^OK\r?\n$' $noStderr

    [System.IO.File]::WriteAllBytes($temporaryFile, [byte[]](0x54, 0x4A, 0x53, 0x32, 0x31, 0x30, 0x30, 0x00))
    Assert-Result 'compiled bytecode file' (Invoke-Checker @($temporaryFile)) 0 '^OK \(compiled bytecode\)\r?\n$' $noStderr

    [System.IO.File]::WriteAllBytes($optionFile, $validScript)
    Assert-Result 'option-like file name' (Invoke-Checker @('--', '-input.tjs') $null $temporaryDirectory) 0 '^OK\r?\n$' $noStderr

    foreach ($lineEnding in @(@("LF", "`n"), @("CR", "`r"), @("CRLF", "`r`n"))) {
        $source = "var valid = 1;$($lineEnding[1])var invalid = ;"
        [System.IO.File]::WriteAllBytes($temporaryFile, $Utf8NoBom.GetBytes($source))
        Assert-Result "invalid $($lineEnding[0]) file" (Invoke-Checker @($temporaryFile)) 1 '^$' ':2:'
    }

    [System.IO.File]::WriteAllBytes($temporaryFile, $validScript)
    $exclusiveHandle = [System.IO.File]::Open($temporaryFile, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
    try {
        Assert-Result 'unreadable file' (Invoke-Checker @($temporaryFile)) 2 '^$' 'cannot open'
    }
    finally {
        $exclusiveHandle.Dispose()
    }
}
finally {
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

Assert-Result 'missing file' (Invoke-Checker @((Join-Path ([System.IO.Path]::GetTempPath()) "missing-tjscheck-$PID.tjs"))) 2 '^$' 'cannot open'

Write-Output 'All tjscheck CLI tests passed.'
