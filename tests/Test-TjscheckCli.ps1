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
        [byte[]]$InputBytes
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Checker
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = $null -ne $InputBytes
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

$warning = 'TJS_NO_REGEXP'
Assert-Result 'help' (Invoke-Checker @('--help')) 0 'TJS_NO_REGEXP' '^$'
Assert-Result 'valid expression' (Invoke-Checker @('-e', '1 + 2 * 3')) 0 '^OK\r?\n$' $warning
Assert-Result 'Unicode expression' (Invoke-Checker @('-e', '"日本語" + "тест"')) 0 '^OK\r?\n$' $warning
Assert-Result 'invalid expression' (Invoke-Checker @('--expression', '1 +')) 1 '^$' '<expression>:1:'
Assert-Result 'regexp expression warning' (Invoke-Checker @('-e', '/abc/i')) 0 '^OK\r?\n$' $warning
Assert-Result 'usage error' (Invoke-Checker @('--stdin', '-e', '1')) 2 '^$' 'Usage:'

$validScript = $Utf8NoBom.GetBytes("var x = 1;`n")
$invalidScript = $Utf8NoBom.GetBytes("var x = ;`n")
$utf16Script = $Utf16LeBom.GetPreamble() + $Utf16LeBom.GetBytes("var x = 1;`n")
Assert-Result 'valid stdin' (Invoke-Checker @('-s') $validScript) 0 '^OK\r?\n$' $warning
Assert-Result 'invalid stdin' (Invoke-Checker @('--stdin') $invalidScript) 1 '^$' '<stdin>:'
Assert-Result 'UTF-16 stdin' (Invoke-Checker @('--stdin') $utf16Script) 0 '^OK\r?\n$' $warning

$temporaryFile = Join-Path ([System.IO.Path]::GetTempPath()) "tjscheck-$PID.tjs"
try {
    [System.IO.File]::WriteAllBytes($temporaryFile, $validScript)
    Assert-Result 'valid file' (Invoke-Checker @($temporaryFile)) 0 '^OK\r?\n$' $warning

    [System.IO.File]::WriteAllBytes($temporaryFile, $invalidScript)
    Assert-Result 'invalid file' (Invoke-Checker @($temporaryFile)) 1 '^$' ':1:'

    [System.IO.File]::WriteAllBytes($temporaryFile, $utf16Script)
    Assert-Result 'UTF-16 file' (Invoke-Checker @($temporaryFile)) 0 '^OK\r?\n$' $warning
}
finally {
    Remove-Item -LiteralPath $temporaryFile -Force -ErrorAction SilentlyContinue
}

Write-Output 'All tjscheck CLI tests passed.'
