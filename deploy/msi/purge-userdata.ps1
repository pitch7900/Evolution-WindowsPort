# Nettoyage des donnees de l'utilisateur qui desinstalle (propriete PURGEUSERDATA=1).
# Source lisible du script embarque (en base64, -EncodedCommand) dans tools\msi\Evolution.wxs
# par tools\msi\embed-scripts.py. Jamais execute lors d'une mise a jour.
# Journal des suppressions : %TEMP%\EvolutionWin-purge.log
$ErrorActionPreference = 'SilentlyContinue'
$journal = Join-Path $env:TEMP 'EvolutionWin-purge.log'
Add-Content $journal ('Nettoyage des donnees EvolutionWin : ' + (Get-Date -Format s))
$dossiers = @(
    ($env:LOCALAPPDATA + '\EvolutionWin'),
    ($env:LOCALAPPDATA + '\evolution'),
    ($env:LOCALAPPDATA + '\Microsoft\Windows\INetCache\evolution')
)
foreach ($d in $dossiers) {
    if (Test-Path $d) {
        Remove-Item $d -Recurse -Force
        Add-Content $journal ('Dossier supprime : ' + $d)
    }
}
# identifiants du Gestionnaire d'identifiants Windows (Evolution-Data-Server:*)
cmdkey /list | Select-String -Pattern 'Evolution-Data-Server:\S+' -AllMatches |
    ForEach-Object { $_.Matches } |
    ForEach-Object {
        cmdkey ('/delete:' + $_.Value) | Out-Null
        Add-Content $journal ('Identifiant supprime : ' + $_.Value)
    }
