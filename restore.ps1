<#
.SYNOPSIS
Restore a full board backup, replacing firmware, settings and history.
.EXAMPLE
.\restore.ps1 -Port COM12 -Backup .\backups\my-board.zip
#>
[CmdletBinding()]
param(
    [string]$Port,
    [string]$Backup
)

$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot
. (Join-Path $PSScriptRoot 'scripts/platformio-common.ps1')
if ([string]::IsNullOrWhiteSpace($Backup)) {
    $Backup = Read-Host 'Backup ZIP file to restore'
}
if ([string]::IsNullOrWhiteSpace($Backup)) { throw 'A backup file is required.' }
$python = Find-PlatformIOPython
$Port = Read-BoardPort $Port
Invoke-Checked $python @((Join-Path $PSScriptRoot 'scripts/board_backup.py'), 'restore', '--port', $Port, '--backup', $Backup)
