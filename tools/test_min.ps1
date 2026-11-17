# tools/test_min.ps1 - map the RR_MINIMAL driver variant into the running game.
#
# Waits for "ROB RUN.exe" to show up (1 s poll, 300 s timeout), gives the game
# 15 s to settle, then runs kdmapper on build_driver\RrSvc_min.sys with
# --copy-header --PassAllocationPtr. Nothing else is started: no overlay, no
# service, no other driver.
#
# stdout, stderr and the exit code of kdmapper are captured and appended,
# timestamped, to build_driver\test_min.log.
#
# kdmapper talks to \\.\IQVW64E, so the terminal MUST be elevated.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\noi\Downloads\rojzqjhezq\tools\test_min.ps1
#
# Script exit codes:
#   0 kdmapper returned 0 (driver mapped)
#   1 timeout while waiting for the game
#   2 terminal not elevated
#   3 prerequisite missing (driver or kdmapper exe)
#   4 kdmapper returned a non-zero code

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- absolute paths: nothing is resolved against the current directory --------
$RepoRoot    = 'C:\Users\noi\Downloads\rojzqjhezq'
$BuildDir    = "$RepoRoot\build_driver"
$DriverPath  = "$BuildDir\RrSvc_min.sys"
$KdmapperExe = "$RepoRoot\tools\kdmapper\x64\Release\kdmapper_Release.exe"
$LogPath     = "$BuildDir\test_min.log"
$StdOutPath  = "$BuildDir\test_min.stdout.txt"
$StdErrPath  = "$BuildDir\test_min.stderr.txt"

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
        '==== test_min.ps1 ====',
        "started : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
        "repo    : $RepoRoot",
        "driver  : $DriverPath",
        "kdmapper: $KdmapperExe",
        "game    : $GameImage",
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

# --- 1. elevation is mandatory for kdmapper -----------------------------------
$principal = [Security.Principal.WindowsPrincipal]::new(
                 [Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole(
               [Security.Principal.WindowsBuiltInRole]::Administrator)

Log "test_min.ps1 start (pid $PID, user $($env:USERNAME), elevated=$isAdmin)"
if (-not $isAdmin) {
    Log 'STOP : terminal non eleve, kdmapper exige les droits administrateur.'
    Log '       relancer le terminal (ou VS Code) via "Run as administrator".'
    Save-Log
    exit 2
}

# --- 2. prerequisites ---------------------------------------------------------
foreach ($req in @($DriverPath, $KdmapperExe)) {
    if (-not (Test-Path -LiteralPath $req)) {
        Log "STOP : introuvable -> $req"
        Save-Log
        exit 3
    }
}
Log ("driver   : {0} ({1} octets)" -f $DriverPath, (Get-Item -LiteralPath $DriverPath).Length)
Log  "kdmapper : $KdmapperExe"

# --- 3. wait for the game: 1 s poll, 300 s timeout ----------------------------
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

# --- 4. settle margin ---------------------------------------------------------
Log "marge de ${MarginSec}s avant mapping"
Start-Sleep -Seconds $MarginSec

# --- 5. map -------------------------------------------------------------------
$kdmArgs = @('--copy-header', '--PassAllocationPtr', $DriverPath)
Log ("lancement : {0} {1}" -f $KdmapperExe, ($kdmArgs -join ' '))

foreach ($f in @($StdOutPath, $StdErrPath)) {
    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
}

$started = Get-Date
$proc = Start-Process -FilePath $KdmapperExe -ArgumentList $kdmArgs -PassThru -Wait `
                      -NoNewWindow -RedirectStandardOutput $StdOutPath `
                      -RedirectStandardError $StdErrPath
$seconds = ((Get-Date) - $started).TotalSeconds
$code = $proc.ExitCode
$hex  = '0x{0:X8}' -f [uint32]([int64]$code -band [int64]0xFFFFFFFFL)

# --- 6. capture stdout, stderr and exit code ----------------------------------
Log '--- kdmapper stdout ---'
if (Test-Path -LiteralPath $StdOutPath) {
    foreach ($line in @(Get-Content -LiteralPath $StdOutPath)) {
        if ($line.Trim().Length -gt 0) { Log ('out| ' + $line) }
    }
}
Log '--- kdmapper stderr ---'
if (Test-Path -LiteralPath $StdErrPath) {
    foreach ($line in @(Get-Content -LiteralPath $StdErrPath)) {
        if ($line.Trim().Length -gt 0) { Log ('err| ' + $line) }
    }
}
Log ('--- kdmapper exit code = {0} ({1}) apres {2:N1}s ---' -f $code, $hex, $seconds)
Log 'aucun overlay lance, aucun autre driver touche.'
Save-Log

Write-Host ("kdmapper exit code = {0} ({1})" -f $code, $hex)
Write-Host ("log -> $LogPath")

if ($code -eq 0) { exit 0 }
exit 4

