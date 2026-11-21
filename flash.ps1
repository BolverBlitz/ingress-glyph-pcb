[CmdletBinding()]
param(
    [string]$Port,
    [int]$Revision,
    [ValidateSet('en', 'de', 'ja')][string]$Language,
    [ValidateSet('res', 'enl', 'resistance', 'enlightened')][string]$Faction,
    [switch]$SkipWifi,
    [switch]$FactoryReset
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

. (Join-Path $PSScriptRoot 'scripts/platformio-common.ps1')

$python = Find-PlatformIOPython
Write-Host "Using Python: $python"

$Port = Read-BoardPort $Port

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

if (-not $FactoryReset) {
    if ([string]::IsNullOrWhiteSpace($Faction)) {
        do {
            $factionAnswer = (Read-Host 'Faction: res/resistance (blue), enl/enlightened (green) [leave blank to keep current]').Trim().ToLowerInvariant()
        } while ($factionAnswer -and $factionAnswer -notin @('res', 'enl', 'resistance', 'enlightened'))
        if ($factionAnswer) { $Faction = $factionAnswer }
    }
    if ($Faction) { $Faction = $Faction.ToLowerInvariant() }
    if ($Faction -eq 'res') { $Faction = 'resistance' }
    if ($Faction -eq 'enl') { $Faction = 'enlightened' }
}

$ssid = $null
$wifiPassword = ''
if (-not $SkipWifi -and -not $FactoryReset) {
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

    if ($FactoryReset) {
        Write-Host "Erasing all flash on $Port (settings, Wi-Fi, drawings, friends, and history)..." -ForegroundColor Yellow
        Invoke-Checked $python @('-m', 'platformio', 'run', '-e', $buildEnvironment, '--target', 'erase', '--upload-port', $Port)
    }

    Write-Host "Flashing $Port..." -ForegroundColor Cyan
    Invoke-Checked $python @('-m', 'platformio', 'run', '-e', $buildEnvironment, '--target', 'upload', '--upload-port', $Port)

    $configureArguments = @(
        'scripts/configure_serial.py',
        '--port', $Port,
        '--revision', [string]$Revision
    )
    if (-not $FactoryReset -and $Faction) {
        $configureArguments += @('--faction', $Faction)
    }
    if ($null -ne $ssid) {
        $configureArguments += @('--ssid', $ssid)
        $env:INGRESS_WIFI_PASSWORD = $wifiPassword
    }

    Write-Host 'Saving controller configuration...' -ForegroundColor Cyan
    Invoke-Checked $python $configureArguments
    if ($FactoryReset) {
        Write-Host "Factory reset completed. All previous data removed; hardware revision $Revision saved." -ForegroundColor Green
    } else {
        Write-Host 'Build, flash, and configuration completed.' -ForegroundColor Green
    }
}
finally {
    Remove-Item Env:INGRESS_WIFI_PASSWORD -ErrorAction SilentlyContinue
    $wifiPassword = ''
    Pop-Location
}
