<#
.SYNOPSIS
    Analyse des repères chronométrés du démarrage d'EvolutionWin (evo-core.log).

.DESCRIPTION
    Le moteur et l'interface écrivent des lignes « démarrage +1234 ms : étape » dans
    %LOCALAPPDATA%\EvolutionWin\evo-core.log (millisecondes depuis la création du
    processus). Ce script les extrait, affiche pour chaque étape le temps cumulé et
    la durée depuis l'étape précédente, puis désigne les étapes les plus longues.

    Usage : lancer EvolutionWin, attendre l'affichage de la liste des messages, puis
    exécuter ce script (le journal de la session précédente est conservé dans
    evo-core.log.old).

.PARAMETER Log
    Chemin du journal (par défaut : %LOCALAPPDATA%\EvolutionWin\evo-core.log).

.PARAMETER Top
    Nombre d'étapes les plus longues à mettre en évidence (par défaut : 5).
#>

param(
    [string]$Log = (Join-Path $env:LOCALAPPDATA 'EvolutionWin\evo-core.log'),
    [int]$Top = 5
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Log)) {
    Write-Error "Journal introuvable : $Log (lancez d'abord EvolutionWin)."
    exit 1
}

$pattern = 'démarrage \+(\d+) ms : (.+)$'
$steps = @()
$previous = 0
foreach ($line in Get-Content -Path $Log -Encoding UTF8) {
    if ($line -match $pattern) {
        $total = [int64]$Matches[1]
        $steps += [pscustomobject]@{
            'Cumul (ms)' = $total
            'Etape (ms)' = $total - $previous
            'Etape'      = $Matches[2]
        }
        $previous = $total
    }
}

if ($steps.Count -eq 0) {
    Write-Warning 'Aucun repère « démarrage » dans le journal : utilisez une version qui contient evo_core_trace.'
    exit 1
}

Write-Host "`n=== Démarrage d'EvolutionWin : $($steps.Count) repères ===" -ForegroundColor Cyan
$steps | Format-Table -AutoSize

Write-Host "=== Les $Top étapes les plus longues ===" -ForegroundColor Cyan
$steps | Sort-Object 'Etape (ms)' -Descending | Select-Object -First $Top | Format-Table -AutoSize

$splash = $steps | Where-Object { $_.Etape -eq 'interface : splash affiché' } | Select-Object -First 1
$list = $steps | Where-Object { $_.Etape -like '*première liste de messages affichée*' } | Select-Object -First 1
if ($splash) { Write-Host ("Splash visible après   : {0} ms" -f $splash.'Cumul (ms)') }
if ($list) { Write-Host ("Première liste affichée : {0} ms" -f $list.'Cumul (ms)') }
