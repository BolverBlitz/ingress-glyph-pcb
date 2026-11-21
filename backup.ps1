<#
.SYNOPSIS
Back up the board's entire flash, including firmware, settings and history.
.EXAMPLE
.\backup.ps1 -Port COM12 -Output .\backups\my-board.zip
#>
[CmdletBinding()]
param(
    [string]$Port,
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot
. (Join-Path $PSScriptRoot 'scripts/platformio-common.ps1')
$python = Find-PlatformIOPython
$Port = Read-BoardPort $Port
$commandArguments = @((Join-Path $PSScriptRoot 'scripts/board_backup.py'), 'backup', '--port', $Port)
if ($Output) { $commandArguments += @('--output', $Output) }
Invoke-Checked $python $commandArguments
