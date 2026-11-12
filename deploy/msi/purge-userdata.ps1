# Nettoyage des donnees de l'utilisateur qui desinstalle (propriete PURGEUSERDATA=1).
# Source lisible du script passe en base64 (-EncodedCommand) a l'action WixQuietExec
# « PurgeUserData » de tools\msi\Evolution.wxs par tools\build-msi.ps1.
# Jamais execute lors d'une mise a jour.
# Chaque element supprime est ecrit sur la sortie standard (recopiee dans le journal MSI
# /l*v par WixQuietExec) et dans le journal %TEMP%\EvolutionWin-purge.log.
# Variables d'environnement posees par le MSI : EVO_LOCALAPPDATA (dossier de donnees locales
# de l'utilisateur), EVO_PURGELOG (chemin du journal).
$ErrorActionPreference = 'SilentlyContinue'
$local = $env:EVO_LOCALAPPDATA
if (-not $local) { $local = $env:LOCALAPPDATA }
$local = $local.TrimEnd('\')
$journal = $env:EVO_PURGELOG
if (-not $journal) { $journal = Join-Path $env:TEMP 'EvolutionWin-purge.log' }

# @brief Ecrit une ligne sur la sortie standard (journal MSI) et dans le journal de purge.
function Ecrire([string]$ligne) {
    Write-Output ('EvolutionWin purge : ' + $ligne)
    Add-Content $journal $ligne
}

Ecrire ('Nettoyage des donnees EvolutionWin : ' + (Get-Date -Format s))
$dossiers = @(
    ($local + '\EvolutionWin'),
    ($local + '\evolution'),
    ($local + '\Microsoft\Windows\INetCache\evolution')
)
foreach ($d in $dossiers) {
    if (Test-Path $d) {
        Remove-Item $d -Recurse -Force
        Ecrire ('Dossier supprime : ' + $d)
    } else {
        Ecrire ('Dossier absent : ' + $d)
    }
}
# identifiants du Gestionnaire d'identifiants Windows (Evolution-Data-Server:*)
cmdkey /list | Select-String -Pattern 'Evolution-Data-Server:\S+' -AllMatches |
    ForEach-Object { $_.Matches } |
    ForEach-Object {
        cmdkey ('/delete:' + $_.Value) | Out-Null
        Ecrire ('Identifiant supprime : ' + $_.Value)
    }
Ecrire 'Nettoyage termine.'
