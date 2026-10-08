# Usage: pwsh measure-commit.ps1 -Php <php.exe> -Mode fiber|coroutine -Count <n> [-PhpArgs <extra php args>]
# Starts commit.php with 0 and with <n> suspended fibers or coroutines, reads the process's private bytes
# (its commit charge) once it prints "ready", and prints the commit per fiber or coroutine.
param(
    [Parameter(Mandatory)] [string] $Php,
    [string] $Mode = 'fiber',
    [int] $Count = 2000,
    [string[]] $PhpArgs = @()
)

$script = Join-Path $PSScriptRoot 'commit.php'

function Measure-Private([int] $n) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Php
    foreach ($arg in $PhpArgs + @($script, $Mode, "$n")) { $info.ArgumentList.Add($arg) }
    $info.RedirectStandardOutput = $true
    $info.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($info)

    try {
        $line = $process.StandardOutput.ReadLine()
        if ($line -ne 'ready') { throw "commit.php printed '$line' instead of ready (exit $($process.ExitCode))" }
        $process.Refresh()
        return $process.PrivateMemorySize64
    } finally {
        if (-not $process.HasExited) { $process.Kill() }
    }
}

$empty = Measure-Private 0
$full = Measure-Private $Count
$perUnit = ($full - $empty) / $Count / 1KB
"{0} x{1}: private bytes {2:N0} MiB (empty run {3:N0} MiB), {4:N1} KiB per {0}" -f $Mode, $Count, ($full / 1MB), ($empty / 1MB), $perUnit
