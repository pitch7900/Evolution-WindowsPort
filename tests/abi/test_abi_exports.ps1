<#
.SYNOPSIS
    Validation de non-régression de l'ABI et des symboles exportés par evo-core.dll.

.DESCRIPTION
    Ce script vérifie que l'ensemble des fonctions publiques déclarées dans winui\core\evo-core.h
    sont effectivement exportées par la DLL compilée build\evo-core.dll.
    Il garantit qu'aucune régression de lien (LNK4044, symbole manquant ou non exporté sous MSVC)
    ne survient entre deux versions du projet.

.PARAMETER DllPath
    Chemin vers evo-core.dll (par défaut: build\evo-core.dll).

.PARAMETER HeaderPath
    Chemin vers l'en-tête evo-core.h (par défaut: winui\core\evo-core.h).
#>

param(
    [string]$DllPath = "$PSScriptRoot\..\..\build\evo-core.dll",
    [string]$HeaderPath = "$PSScriptRoot\..\..\winui\core\evo-core.h"
)

$ErrorActionPreference = "Stop"

Write-Host "`n======================================================" -ForegroundColor Cyan
Write-Host "  VALIDATION ABI : Symboles exportes de evo-core.dll" -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan

if (-not (Test-Path $HeaderPath)) {
    Write-Error "Fichier d'en-tete introuvable : $HeaderPath"
    exit 1
}

if (-not (Test-Path $DllPath)) {
    Write-Warning "DLL introuvable : $DllPath. La DLL doit etre prealablement compilee."
    exit 1
}

# 1. Extraction des symboles attendus depuis evo-core.h
$headerContent = Get-Content -Path $HeaderPath -Raw
$pattern = '(?m)^\s*([a-zA-Z0-9_]+(\s*\*+)?)\s+(evo_core_[a-zA-Z0-9_]+)\s*\('
$matches = [regex]::Matches($headerContent, $pattern)

$expectedSymbols = @()
foreach ($m in $matches) {
    $funcName = $m.Groups[3].Value
    if ($funcName -and -not ($expectedSymbols -contains $funcName)) {
        $expectedSymbols += $funcName
    }
}

Write-Host "  Symboles publics attendus (evo-core.h) : $($expectedSymbols.Count)" -ForegroundColor Gray

# 2. Extraction des exports de la DLL via dumpbin
$dumpbinOutput = & cmd /c "`"$PSScriptRoot\..\..\tools\env.bat`" >nul 2>&1 && dumpbin /exports `"$DllPath`""
$exportedSymbols = @()

foreach ($line in $dumpbinOutput) {
    if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(evo_core_\w+)') {
        $exportedSymbols += $matches[1]
    }
}

Write-Host "  Symboles trouves dans la DLL           : $($exportedSymbols.Count)" -ForegroundColor Gray

# 3. Comparaison et detection des manques
$missingSymbols = @()
foreach ($sym in $expectedSymbols) {
    if ($exportedSymbols -notcontains $sym) {
        $missingSymbols += $sym
    }
}

Write-Host "------------------------------------------------------" -ForegroundColor Gray

if ($missingSymbols.Count -eq 0) {
    Write-Host "  SUCCES : Les $($expectedSymbols.Count) fonctions d'evo-core.h sont parfaitement exportees !" -ForegroundColor Green
    Write-Host "======================================================`n" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host "  ECHEC : $($missingSymbols.Count) symbole(s) manquant(s) dans la DLL :" -ForegroundColor Red
    foreach ($m in $missingSymbols) {
        Write-Host "    - $m" -ForegroundColor Red
    }
    Write-Host "======================================================`n" -ForegroundColor Cyan
    exit 1
}

