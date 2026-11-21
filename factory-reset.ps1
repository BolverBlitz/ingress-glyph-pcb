<#
.SYNOPSIS
Erase all saved board data and reinstall firmware with factory defaults.
.DESCRIPTION
Builds before erasing the entire flash, including NVS, LittleFS, backup files,
and both firmware slots. Prompts for hardware revision and saves that selection
after reinstalling firmware. Wi-Fi and other settings use factory defaults.
Close serial monitors first.
If upload fails after erasing, rerun this script to restore the firmware.
.EXAMPLE
.\factory-reset.ps1 -Port COM12
.EXAMPLE
.\factory-reset.ps1 -Port 12 -Revision 2 -Language de
#>
[CmdletBinding()]
param(
    [string]$Port,
    [int]$Revision,
    [ValidateSet('en', 'de', 'ja')][string]$Language = 'en'
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'flash.ps1') -Port $Port -Revision $Revision -Language $Language -FactoryReset
