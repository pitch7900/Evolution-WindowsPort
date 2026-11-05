# Assemble winui\app\MainWindow.xaml à partir du gabarit winui\app\xaml\MainWindow.xaml.in et des
# fragments winui\app\xaml\*.xamlpart.
#
# Pourquoi : MainWindow.xaml dépassait 5 800 lignes. WinUI 3 n'a pas d'inclusion de XAML, et des
# UserControl casseraient l'accès aux éléments nommés (x:Name) depuis les MainWindow.*.cpp ; le fichier
# est donc découpé en sources plus petites, réassemblées à l'identique avant la compilation XAML.
#
# Usage : powershell -NoProfile -ExecutionPolicy Bypass -File tools\assemble-xaml.ps1 -AppDir winui\app
# (appelé automatiquement par EvolutionWin.vcxproj au début de chaque build ; MainWindow.xaml est ignoré
# par git : on modifie le gabarit et les fragments, jamais le fichier généré.)
# Le fichier généré n'est réécrit que s'il change, pour ne pas relancer inutilement la compilation XAML.
param(
    [Parameter(Mandatory = $true)][string]$AppDir
)

$ErrorActionPreference = 'Stop'

$xamlDir = Join-Path $AppDir 'xaml'
$templatePath = Join-Path $xamlDir 'MainWindow.xaml.in'
$targetPath = Join-Path $AppDir 'MainWindow.xaml'

if (-not (Test-Path -LiteralPath $templatePath)) {
    throw "Gabarit introuvable : $templatePath"
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$template = $utf8.GetString([IO.File]::ReadAllBytes($templatePath))

$pattern = '<!--#include (xaml/[A-Za-z0-9_.\-]+\.xamlpart)-->'
$assembled = [regex]::Replace($template, $pattern, {
        param($match)
        $fragmentPath = Join-Path $AppDir $match.Groups[1].Value
        if (-not (Test-Path -LiteralPath $fragmentPath)) {
            throw "Fragment introuvable : $fragmentPath (inclus par $templatePath)"
        }
        $utf8.GetString([IO.File]::ReadAllBytes($fragmentPath))
    })

$newBytes = $utf8.GetBytes($assembled)
$unchanged = $false
if (Test-Path -LiteralPath $targetPath) {
    $oldBytes = [IO.File]::ReadAllBytes($targetPath)
    if ($oldBytes.Length -eq $newBytes.Length) {
        $unchanged = $true
        for ($i = 0; $i -lt $oldBytes.Length; $i++) {
            if ($oldBytes[$i] -ne $newBytes[$i]) { $unchanged = $false; break }
        }
    }
}

if (-not $unchanged) {
    [IO.File]::WriteAllBytes($targetPath, $newBytes)
    Write-Host "MainWindow.xaml assemblé ($($newBytes.Length) octets)."
}
