<#
.SYNOPSIS
Self-play bench: records AI-only games for several advisor variants, seeds and difficulties.

.DESCRIPTION
Runs a debug build of orion2re with /oracle-record for every combination of variant, seed and
difficulty. Each variant is a set of config lines appended to a copy of the game's main config,
written as data\selfplay-<variant>.cfg (configs are read through the game's virtual file system)
and passed with /config; the copies are removed at the end. With "advisor journal = full" in a variant, the advisor journal of the
game is moved to <OutDir>/d<difficulty>-s<seed>-<variant>.jsonl; tools/selfplay/report.py
compares the variants from those journals.

The "observe" variant turns the advisor on with all its mechanics off: the game plays exactly as
without the advisor (the advisor never draws from the game RNG), but the journal is written.

.EXAMPLE
pwsh tools/selfplay/selfplay.ps1 -GameDir D:\moo2\game -OutDir D:\bench -Seeds 1..8 -Difficulties 2,3
#>
param(
    [Parameter(Mandatory = $true)] [string] $GameDir,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [int[]] $Seeds = @(1, 2, 3, 4, 5, 6, 7, 8),
    [int[]] $Difficulties = @(2, 3),
    [int] $Turns = 100,
    [string] $Exe = "orion2re.exe",
    [System.Collections.IDictionary] $Variants = $null
)

$ErrorActionPreference = 'Stop'
$on = "advisor enabled = true;`nadvisor journal = full;"
if ($null -eq $Variants) {
    $Variants = [ordered]@{
        "observe" = "$on`nadvisor rules_objectives = false;`nadvisor rules_spies = false;`nadvisor rules_expansion = false;`nadvisor rules_outposts = false;`nadvisor rules_exploration = false;`nadvisor rules_lanchester = false;`nadvisor rules_balance = false;"
        "advisor" = $on
    }
}

$exePath = Join-Path $GameDir $Exe
$baseConfig = Join-Path $GameDir "data\orion2re.cfg"
if (-not (Test-Path $exePath)) { throw "game executable not found: $exePath" }
if (-not (Test-Path $baseConfig)) { throw "main config not found: $baseConfig" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$journalDir = Join-Path $GameDir "advisor_journal"
$historyRoot = Join-Path $OutDir "history"

foreach ($difficulty in $Difficulties) {
    foreach ($seed in $Seeds) {
        foreach ($name in $Variants.Keys) {
            $tag = "d$difficulty-s$seed-$name"
            $configName = "selfplay-$name.cfg"
            $config = Join-Path $GameDir "data\$configName"
            Copy-Item $baseConfig $config -Force
            Add-Content $config "`n$($Variants[$name])`n"
            $history = Join-Path $historyRoot $tag
            if (Test-Path $history) { Remove-Item -Recurse -Force $history }
            New-Item -ItemType Directory -Force $historyRoot | Out-Null
            if (Test-Path $journalDir) { Remove-Item -Recurse -Force $journalDir }
            $arguments = @("/config", $configName, "/oracle-record", "`"$history`"", "/oracle-turns", "$Turns",
                "/oracle-seed", "$seed", "/oracle-difficulty", "$difficulty")
            $process = Start-Process $exePath -ArgumentList $arguments -WorkingDirectory $GameDir `
                -RedirectStandardOutput (Join-Path $OutDir "$tag.out") -RedirectStandardError (Join-Path $OutDir "$tag.err") `
                -PassThru -Wait
            "$tag exit $($process.ExitCode)"
            Get-ChildItem $journalDir -Filter *.jsonl -ErrorAction SilentlyContinue |
                ForEach-Object { Move-Item $_.FullName (Join-Path $OutDir "$tag.jsonl") -Force }
            # Oracle histories are large and not needed for the comparison.
            Remove-Item -Recurse -Force $history -ErrorAction SilentlyContinue
        }
    }
}
foreach ($name in $Variants.Keys) {
    Remove-Item (Join-Path $GameDir "data\selfplay-$name.cfg") -ErrorAction SilentlyContinue
}
