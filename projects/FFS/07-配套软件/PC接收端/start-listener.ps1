param(
    [int]$Port = 7777,
    [string]$DataRoot = (Join-Path $PSScriptRoot 'data\sensors'),
    [switch]$ForwardToFlitFancy
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'scripts\vendor\listen_wifi.ps1') -Port $Port -DataRoot $DataRoot -NoFlitFancy:(-not $ForwardToFlitFancy)
