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
    [switch] $Tactical,
    [switch] $Visible,
    [System.Collections.IDictionary] $Variants = $null
)

$ErrorActionPreference = 'Stop'
$on = "advisor enabled = true;`nadvisor journal = full;"
if ($null -eq $Variants) {
    $Variants = [ordered]@{
        "observe" = "$on`nadvisor rules_objectives = false;`nadvisor rules_spies = false;`nadvisor rules_expansion = false;`nadvisor rules_outposts = false;`nadvisor rules_exploration = false;`nadvisor rules_lanchester = false;`nadvisor rules_balance = false;`nadvisor rules_counter_design = false;`nadvisor rules_doctrine = false;"
        "advisor" = $on
    }
}

# Games start minimized in the taskbar without taking the keyboard focus (SW_SHOWMINNOACTIVE), so
# a bench does not interrupt other work; -Visible starts them as usual. PowerShell's Start-Process
# can only ask for SW_SHOWMINIMIZED, which still activates the window.
if (-not $Visible -and -not ("SelfplayQuietLaunch" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SelfplayQuietLaunch {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public bool bInheritHandle; }
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcess(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
        IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFile(string name, uint access, uint share, ref SECURITY_ATTRIBUTES sa, uint disposition,
        uint flags, IntPtr template);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    static IntPtr Open_(string path) {
        var sa = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)), bInheritHandle = true };
        IntPtr handle = CreateFile(path, 0x40000000 /* GENERIC_WRITE */, 3 /* share read, write */, ref sa,
            2 /* CREATE_ALWAYS */, 0x80 /* FILE_ATTRIBUTE_NORMAL */, IntPtr.Zero);
        if (handle == new IntPtr(-1)) throw new System.ComponentModel.Win32Exception();
        return handle;
    }

    // Runs `exe arguments` in `dir` minimized and not activated, stdout and stderr to files; returns the exit code.
    public static int Run(string exe, string arguments, string dir, string outPath, string errPath) {
        IntPtr output = Open_(outPath);
        IntPtr error = Open_(errPath);
        try {
            var si = new STARTUPINFO();
            si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
            si.dwFlags = 0x1 /* STARTF_USESHOWWINDOW */ | 0x100 /* STARTF_USESTDHANDLES */;
            si.wShowWindow = 7; /* SW_SHOWMINNOACTIVE */
            si.hStdOutput = output;
            si.hStdError = error;
            PROCESS_INFORMATION pi;
            var command = new StringBuilder("\"" + exe + "\" " + arguments);
            if (!CreateProcess(null, command, IntPtr.Zero, IntPtr.Zero, true, 0, IntPtr.Zero, dir, ref si, out pi)) {
                throw new System.ComponentModel.Win32Exception();
            }
            WaitForSingleObject(pi.hProcess, 0xFFFFFFFF);
            uint code;
            GetExitCodeProcess(pi.hProcess, out code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return unchecked((int)code);
        } finally {
            CloseHandle(output);
            CloseHandle(error);
        }
    }
}
"@
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
            if ($Tactical) { $arguments += "/oracle-tactical" }
            if ($Visible) {
                $process = Start-Process $exePath -ArgumentList $arguments -WorkingDirectory $GameDir `
                    -RedirectStandardOutput (Join-Path $OutDir "$tag.out") -RedirectStandardError (Join-Path $OutDir "$tag.err") `
                    -PassThru -Wait
                $exitCode = $process.ExitCode
            } else {
                $exitCode = [SelfplayQuietLaunch]::Run($exePath, ($arguments -join " "), $GameDir,
                    (Join-Path $OutDir "$tag.out"), (Join-Path $OutDir "$tag.err"))
            }
            "$tag exit $exitCode"
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
