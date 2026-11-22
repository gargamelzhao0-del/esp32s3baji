# Compatibility entry point for existing local build commands.
[CmdletBinding()]
param(
    [string]$IdfPath,
    [string]$ToolsRoot
)
& (Join-Path $PSScriptRoot 'build.ps1') @PSBoundParameters
