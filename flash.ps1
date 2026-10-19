[CmdletBinding()]
param(
    [string]$Port,
    [int]$Revision,
    [ValidateSet('en', 'de', 'ja')][string]$Language,
    [switch]$SkipWifi
)

$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot

function Read-PlainSecureString {
    param([Security.SecureString]$SecureValue)
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureValue)
    try {
        [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
}

function Invoke-Checked {
    param([string]$Executable, [string[]]$CommandArguments)
    & $Executable @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($CommandArguments -join ' ')"
    }
}

function Find-PlatformIOPython {
    # A Python launcher can select a different installation from the one that
    # has PlatformIO. Prefer virtual environments, then check PATH candidates.
    $candidates = @(
        if ($env:VIRTUAL_ENV) { Join-Path $env:VIRTUAL_ENV 'Scripts\python.exe' }
        Join-Path $projectDirectory '.pio\venv\Scripts\python.exe'
        Join-Path $projectDirectory '.venv\Scripts\python.exe'
        Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
        Get-Command python, py -All -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Source
    )
    $probe = "import importlib.util, sys; ready = all(importlib.util.find_spec(name) is not None for name in ('platformio', 'serial')); print(sys.executable) if ready else None; sys.exit(0 if ready else 1)"
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        try {
            $resolvedPython = & $candidate -c $probe 2>$null
            if ($LASTEXITCODE -eq 0 -and $resolvedPython) {
                return ($resolvedPython | Select-Object -Last 1).Trim()
            }
        }
        catch {
            # Try the next installation if this interpreter cannot start.
        }
    }
    throw 'No Python installation with PlatformIO and pyserial was found. Run python -m pip install platformio pyserial, then retry.'
}

$python = Find-PlatformIOPython
Write-Host "Using Python: $python"

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Read-Host 'Serial port or number [COM11]'
    if ([string]::IsNullOrWhiteSpace($Port)) { $Port = 'COM11' }
}
$Port = $Port.Trim()
if ($Port -match '^\d+$') { $Port = "COM$Port" }

$availablePorts = [IO.Ports.SerialPort]::GetPortNames()
if ($availablePorts -notcontains $Port) {
    $availableText = if ($availablePorts.Count) {
        $availablePorts -join ', '
    } else {
        '<none>'
    }
    throw "Serial port $Port was not found. Available ports: $availableText"
}

while ($Revision -ne 1 -and $Revision -ne 2) {
    $answer = Read-Host 'Hardware revision (1 or 2)'
    if ([int]::TryParse($answer, [ref]$Revision) -and
        ($Revision -eq 1 -or $Revision -eq 2)) {
        break
    }
    Write-Host 'Please enter 1 or 2.' -ForegroundColor Yellow
}

if ([string]::IsNullOrWhiteSpace($Language)) {
    do {
        $Language = (Read-Host 'UI language: en (English), de (Deutsch), ja (日本語) [en]').Trim().ToLowerInvariant()
        if (-not $Language) { $Language = 'en' }
    } while ($Language -notin @('en', 'de', 'ja'))
}
$Language = $Language.ToLowerInvariant()
$buildEnvironment = "esp32-c3-devkitm-1-$Language"

$ssid = $null
$wifiPassword = ''
if (-not $SkipWifi) {
    $ssidAnswer = Read-Host 'Wi-Fi SSID (leave blank to skip Wi-Fi setup)'
    if (-not [string]::IsNullOrWhiteSpace($ssidAnswer)) {
        $ssid = $ssidAnswer
        $securePassword = Read-Host 'Wi-Fi password (leave blank for an open network)' -AsSecureString
        $wifiPassword = Read-PlainSecureString $securePassword
    }
}

Push-Location $projectDirectory
try {
    Write-Host "Building $Language firmware..." -ForegroundColor Cyan
    Invoke-Checked $python @('-m', 'platformio', 'run', '-e', $buildEnvironment)

    Write-Host "Flashing $Port..." -ForegroundColor Cyan
    Invoke-Checked $python @('-m', 'platformio', 'run', '-e', $buildEnvironment, '--target', 'upload', '--upload-port', $Port)

    $configureArguments = @(
        'scripts/configure_serial.py',
        '--port', $Port,
        '--revision', [string]$Revision
    )
    if ($null -ne $ssid) {
        $configureArguments += @('--ssid', $ssid)
        $env:INGRESS_WIFI_PASSWORD = $wifiPassword
    }

    Write-Host 'Saving controller configuration...' -ForegroundColor Cyan
    Invoke-Checked $python $configureArguments
    Write-Host 'Build, flash, and configuration completed.' -ForegroundColor Green
}
finally {
    Remove-Item Env:INGRESS_WIFI_PASSWORD -ErrorAction SilentlyContinue
    $wifiPassword = ''
    Pop-Location
}
