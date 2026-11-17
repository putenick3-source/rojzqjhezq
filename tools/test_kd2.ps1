# tools/test_kd2.ps1 - map the RR_MINIMAL driver variant with the alternative
# mapper kd2.exe (FELONIOUS FUNNERY public release) instead of kdmapper.
#
# Same shape as tools/test_min.ps1: elevation check, wait for "ROB RUN.exe"
# (1 s poll, 300 s timeout), 15 s settle margin, timestamped capture of stdout,
# stderr and exit code into build_driver\test_kd2.log, same exit codes.
#
# Two deliberate differences:
#   - kd2.exe is started with NO argument and from tools\kd2: it maps the
#     "kernel-mode.sys" sitting next to itself (its source reads
#     get_exe_directory() + "\\kernel-mode.sys"), so that image must stay there.
#   - stdin is redirected from a file holding one newline. Without --auto, kd2
#     calls std::cin.get() on its way out (every exit path), which would hang a
#     non-interactive run; the redirect makes those waits return immediately.
#
# kd2 talks to \\.\Nal (iqvw64e), so the terminal MUST be elevated.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\noi\Downloads\rojzqjhezq\tools\test_kd2.ps1
#
# Script exit codes (identical to test_min.ps1):
#   0 kd2 returned 0 (driver mapped)
#   1 timeout while waiting for the game
#   2 terminal not elevated
#   3 prerequisite missing (kd2.exe or kernel-mode.sys)
#   4 kd2 returned a non-zero code
#
# Optional switch:
#   -NoWait  skip the game wait and the settle margin and map immediately. Use it
#            to map BEFORE launching the game: a resident EasyAntiCheat driver
#            makes the iqvw64e load fail with 0xC0000022. Without the switch the
#            behaviour is unchanged (wait for the game, 15 s margin, then map).

param(
    [switch]$NoWait
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- absolute paths: nothing is resolved against the current directory --------
$RepoRoot   = 'C:\Users\noi\Downloads\rojzqjhezq'
$BuildDir   = "$RepoRoot\build_driver"
$DriverDir  = "$RepoRoot\tools\kd2"
$Kd2Exe     = "$DriverDir\kd2.exe"
$DriverPath = "$DriverDir\kernel-mode.sys"
$LogPath    = "$BuildDir\test_kd2.log"
$StdOutPath = "$BuildDir\test_kd2.stdout.txt"
$StdErrPath = "$BuildDir\test_kd2.stderr.txt"
$StdInPath  = "$BuildDir\test_kd2.stdin.txt"

# --- knobs --------------------------------------------------------------------
$GameImage    = 'ROB RUN.exe'   # image name, as requested
$GameProcName = 'ROB RUN'       # Get-Process exposes the name without ".exe"
$TimeoutSec   = 300
$PollSec      = 1
$MarginSec    = 15

$script:LogLines = @()

function Log([string]$Message) {
    $line = '[' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss') + '] ' + $Message
    $script:LogLines += $line
    Write-Host $line
}

function Save-Log {
    $head = @(
        '==== test_kd2.ps1 ====',
        "started : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
        "repo    : $RepoRoot",
        "kd2     : $Kd2Exe",
        "driver  : $DriverPath",
        "game    : $GameImage",
        "nowait  : $NoWait",
        '======================'
    )
    Set-Content -LiteralPath $LogPath -Value $head -Encoding UTF8
    if ($script:LogLines.Count -gt 0) {
        Add-Content -LiteralPath $LogPath -Value $script:LogLines -Encoding UTF8
    }
}

# --- 0. log directory ---------------------------------------------------------
if (-not (Test-Path -LiteralPath $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# --- 1. elevation is mandatory for kd2 ----------------------------------------
$principal = [Security.Principal.WindowsPrincipal]::new(
                 [Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole(
               [Security.Principal.WindowsBuiltInRole]::Administrator)

Log "test_kd2.ps1 start (pid $PID, user $($env:USERNAME), elevated=$isAdmin)"
if (-not $isAdmin) {
    Log 'STOP : terminal non eleve, kd2 exige les droits administrateur.'
    Log '       relancer le terminal (ou VS Code) via "Run as administrator".'
    Save-Log
    exit 2
}

# --- 2. prerequisites ---------------------------------------------------------
foreach ($req in @($Kd2Exe, $DriverPath)) {
    if (-not (Test-Path -LiteralPath $req)) {
        Log "STOP : introuvable -> $req"
        Save-Log
        exit 3
    }
}
Log ("kd2      : {0} ({1} octets)" -f $Kd2Exe, (Get-Item -LiteralPath $Kd2Exe).Length)
Log ("driver   : {0} ({1} octets)" -f $DriverPath, (Get-Item -LiteralPath $DriverPath).Length)
Log ("sha256   : {0}" -f (Get-FileHash -LiteralPath $DriverPath -Algorithm SHA256).Hash)

# --- 3. wait for the game: 1 s poll, 300 s timeout ----------------------------
if ($NoWait) {
    # Mapping now: nothing is waited for, the game may not even be up yet.
    $np = Get-Process -ErrorAction SilentlyContinue |
          Where-Object { $_.ProcessName -ieq $GameProcName } |
          Select-Object -First 1
    if ($null -ne $np) {
        Log ("NoWait : mapping immediat, '{0}' deja lance (pid={1})" -f $GameImage, $np.Id)
    } else {
        Log "NoWait : mapping immediat, '$GameImage' non lance"
    }
} else {
    Log "attente de '$GameImage' (poll ${PollSec}s, timeout ${TimeoutSec}s)"
    $waited = 0
    $gamePid = 0
    while ($gamePid -eq 0) {
        $found = Get-Process -ErrorAction SilentlyContinue |
                 Where-Object { $_.ProcessName -ieq $GameProcName } |
                 Select-Object -First 1
        if ($null -ne $found) {
            $gamePid = $found.Id
            break
        }
        if ($waited -ge $TimeoutSec) {
            Log "TIMEOUT : '$GameImage' non detecte apres ${TimeoutSec}s"
            Save-Log
            exit 1
        }
        if (($waited % 15) -eq 0) {
            Log ("attente ... {0}/{1}s - '{2}' pas encore lance" -f $waited, $TimeoutSec, $GameImage)
        }
        Start-Sleep -Seconds $PollSec
        $waited += $PollSec
    }
    Log ("detecte : '{0}' pid={1} apres {2}s" -f $GameImage, $gamePid, $waited)

    # --- 4. settle margin -----------------------------------------------------
    Log "marge de ${MarginSec}s avant mapping"
    Start-Sleep -Seconds $MarginSec
}

# --- 5. map -------------------------------------------------------------------
# No argument: kd2 maps the kernel-mode.sys beside itself.
Log ("lancement : {0}   (cwd {1}, sans argument)" -f $Kd2Exe, $DriverDir)

foreach ($f in @($StdOutPath, $StdErrPath)) {
    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
}
# One CRLF on stdin so every std::cin.get() inside kd2 returns at once.
[IO.File]::WriteAllText($StdInPath, "`r`n")

$started = Get-Date
$proc = Start-Process -FilePath $Kd2Exe -WorkingDirectory $DriverDir -PassThru -Wait `
                      -NoNewWindow -RedirectStandardOutput $StdOutPath `
                      -RedirectStandardError $StdErrPath `
                      -RedirectStandardInput $StdInPath
$seconds = ((Get-Date) - $started).TotalSeconds
$code = $proc.ExitCode
$hex  = '0x{0:X8}' -f [uint32]([int64]$code -band [int64]0xFFFFFFFFL)

# --- 6. capture stdout, stderr and exit code ----------------------------------
Log '--- kd2 stdout ---'
if (Test-Path -LiteralPath $StdOutPath) {
    foreach ($line in @(Get-Content -LiteralPath $StdOutPath)) {
        if ($line.Trim().Length -gt 0) { Log ('out| ' + $line) }
    }
}
Log '--- kd2 stderr ---'
if (Test-Path -LiteralPath $StdErrPath) {
    foreach ($line in @(Get-Content -LiteralPath $StdErrPath)) {
        if ($line.Trim().Length -gt 0) { Log ('err| ' + $line) }
    }
}
Log ('--- kd2 exit code = {0} ({1}) apres {2:N1}s ---' -f $code, $hex, $seconds)
Log 'aucun overlay lance, aucun autre driver touche.'
Save-Log

Write-Host ("kd2 exit code = {0} ({1})" -f $code, $hex)
Write-Host ("log -> $LogPath")

if ($code -eq 0) { exit 0 }
exit 4

