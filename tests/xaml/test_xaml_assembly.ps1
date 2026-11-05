# Test d'assemblage du XAML de la fenêtre principale : le gabarit et les fragments doivent donner un
# MainWindow.xaml XML bien formé, sans inclusion orpheline ni fragment inutilisé.
# Voir tools\assemble-xaml.ps1 et LLM.md (§ 6.2 bis).
# Code de retour : 0 si tout est correct, 1 sinon.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$appDir = Join-Path $root 'winui\app'
$xamlDir = Join-Path $appDir 'xaml'
$failures = 0

function Fail($message) {
    Write-Host "  [ECHEC] $message"
    $script:failures++
}

Write-Host '  [TEST] XAML : assemblage de MainWindow.xaml depuis le gabarit et les fragments ...'

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\assemble-xaml.ps1') -AppDir $appDir
if ($LASTEXITCODE -ne 0) { Fail "tools\assemble-xaml.ps1 a échoué (code $LASTEXITCODE)" }

# 1. XML bien formé
try {
    [xml](Get-Content -LiteralPath (Join-Path $appDir 'MainWindow.xaml') -Raw -Encoding UTF8) | Out-Null
} catch {
    Fail "MainWindow.xaml assemblé n'est pas du XML bien formé : $($_.Exception.Message)"
}

# 2. chaque fragment du dossier est inclus exactement une fois par le gabarit
$template = Get-Content -LiteralPath (Join-Path $xamlDir 'MainWindow.xaml.in') -Raw -Encoding UTF8
$included = [regex]::Matches($template, '<!--#include xaml/([A-Za-z0-9_.\-]+\.xamlpart)-->') | ForEach-Object { $_.Groups[1].Value }
$onDisk = Get-ChildItem -LiteralPath $xamlDir -Filter '*.xamlpart' | ForEach-Object { $_.Name }
foreach ($name in $onDisk) {
    $count = @($included | Where-Object { $_ -eq $name }).Count
    if ($count -ne 1) { Fail "fragment '$name' inclus $count fois (attendu : 1)" }
}
foreach ($name in ($included | Select-Object -Unique)) {
    if ($onDisk -notcontains $name) { Fail "inclusion d'un fragment absent : $name" }
}

# 3. aucun reste de marqueur d'inclusion dans le fichier assemblé
$assembled = Get-Content -LiteralPath (Join-Path $appDir 'MainWindow.xaml') -Raw -Encoding UTF8
if ($assembled -match '<!--#include') { Fail "MainWindow.xaml contient encore un marqueur d'inclusion" }

if ($failures -eq 0) {
    Write-Host "  [TEST] XAML : $($onDisk.Count) fragments, MainWindow.xaml bien formé ... SUCCES"
    exit 0
}
exit 1
