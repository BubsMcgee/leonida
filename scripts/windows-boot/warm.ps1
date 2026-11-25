<#
.SYNOPSIS
    Warm the pipeline cache, then time a scene: two windows-boot runs plus a pass/fail check.

.DESCRIPTION
    First boot (twice as long) populates one pipeline cache shared by every later boot and lets the asynchronous
    pipeline worker and the driver compile the title's pipelines, with the compile workers raised above the title's
    threads so they keep up. The second boot reuses that cache; its log's per-window frame rates and its digest
    (presents, faults, non-black captures) are checked by tools/scene_timing.py.

    Same requirements as windows-boot\boot.ps1 (retail package, one game at a time). Set LEONIDA_GAME_DIR,
    LEONIDA_SHADERS, GTA6RT, LEONIDA_RUNS once as that README says. Nothing is written into the repository.

.EXAMPLE
    powershell -NoProfile -File scripts\windows-boot\warm.ps1 -Name first

.EXAMPLE
    powershell -NoProfile -File scripts\windows-boot\warm.ps1 -Name story -WarmSeconds 300 -CheckSeconds 180 `
        -StartAt 90 -MinFps 4 -Threads 8 -GameDir D:\GTA6pkg -Shaders E:\leonida-work\shaders\spirv
#>
[CmdletBinding()]
param(
    [string]$Name = "warm-$((Get-Date).ToString('yyyyMMdd-HHmmss'))",
    [string]$CacheDir = '',
    [string]$OutDir = '',
    [int]$WarmSeconds = 240,
    [int]$CheckSeconds = 120,
    [int]$StartAt = 60,
    [double]$MinFps = 5.0,
    [int]$Threads = 0,
    [switch]$DryRun,
    [Parameter(ValueFromRemainingArguments = $true)] $BootArgs = @()
)

$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Boot = Join-Path $PSScriptRoot 'boot.ps1'
if (-not (Test-Path -LiteralPath $Boot)) { throw "boot.ps1 not found beside warm.ps1" }

# boot.ps1's output folder default, so this script knows where the log and digest land.
if (-not $OutDir) { $OutDir = if ($env:LEONIDA_RUNS) { $env:LEONIDA_RUNS } else { Join-Path $HOME 'leonida-runs' } }
if (-not $CacheDir) { $CacheDir = Join-Path $OutDir 'pipeline-cache' }

# The compile side of "keep up": one warmed cache, savable during startup, async builds ahead of first use, workers
# above the title's threads. Threads 0 leaves the runtime's automatic count (half the logical processors, at most 8).
$warm = @{
    LEONIDA_PIPELINE_CACHE_DIR          = $CacheDir
    LEONIDA_STARTUP_CACHE_CHECKPOINTS   = '1'
    LEONIDA_ASYNC_PIPELINES             = '1'
    LEONIDA_PIPELINE_PRIORITY           = 'above'
    LEONIDA_SUBMIT_PHASES_SECONDS       = '5'
}
if ($Threads -gt 0) { $warm['LEONIDA_PIPELINE_THREADS'] = "$Threads" }

Write-Host "Repo:   $Repo"
Write-Host "Out:    $OutDir"
Write-Host "Cache:  $CacheDir"
foreach ($k in $warm.Keys) { Write-Host ("  {0}={1}" -f $k, $warm[$k]) }
if ($DryRun) { Write-Host "Dry run: would warm for $WarmSeconds s, then check for $CheckSeconds s."; exit 0 }
New-Item -ItemType Directory -Force $OutDir, $CacheDir | Out-Null

$Run = {
    param([string]$RunName, [int]$Seconds)
    $extra = @('-Name', $RunName, '-Seconds', "$Seconds", '-OutDir', $OutDir, '-SetEnv', $warm) + @($BootArgs)
    & powershell -NoProfile -File $Boot @extra
    if ($LASTEXITCODE -ne 0) { throw "boot.ps1 $RunName exited $LASTEXITCODE" }
}

Write-Host ''
Write-Host "== warm boot: $WarmSeconds s (populates $CacheDir) =="
& $Run "$Name-warm" $WarmSeconds

Write-Host ''
Write-Host "== scene check: $CheckSeconds s on the warmed cache =="
& $Run "$Name-check" $CheckSeconds

$log = Join-Path $OutDir "$Name-check.log"
$digest = Join-Path $OutDir "$Name-check.digest.txt"
if (-not (Test-Path -LiteralPath $log)) { throw "no check log at $log" }
$py = @('tools\scene_timing.py', $log, '--start-at', "$StartAt", '--min-fps', "$MinFps", '--require-frames', '1')
if (Test-Path -LiteralPath $digest) { $py += @('--digest', $digest) }
Push-Location $Repo
try { & python -I @py } finally { Pop-Location }
exit $LASTEXITCODE
