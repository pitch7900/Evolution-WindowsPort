# Fermeture des dependances d'EvolutionWin avant mise a jour / desinstallation.
# Source lisible du script embarque (en base64, -EncodedCommand) dans tools\msi\Evolution.wxs
# par tools\msi\embed-scripts.py : toute modification ici doit etre re-embarquee.
# Execute dans TOUTES les sessions (serveur multi-sessions, session deconnectee) :
# demande de fermeture propre, 5 s de delai, puis arret force.
$ErrorActionPreference = 'SilentlyContinue'
$install = $env:ProgramFiles + '\Evolution\'
$noms = 'EvolutionWin', 'camel-index-control', 'gspawn-win64-helper', 'gspawn-win64-helper-console'
# processus de l'installation uniquement (ceux d'autres applications GLib sont epargnes)
$cibles = Get-Process -Name $noms | Where-Object { $_.Path -like ($install + '*') -or $_.ProcessName -eq 'EvolutionWin' }
$cibles | ForEach-Object { $null = $_.CloseMainWindow() }
if ($cibles) { Start-Sleep -Seconds 5 }
$cibles | Stop-Process -Force
# WebView2 est partage avec d'autres applications : seuls les processus dont la ligne de
# commande vise le dossier de donnees d'EvolutionWin sont arretes
Get-CimInstance Win32_Process |
    Where-Object { $_.Name -eq 'msedgewebview2.exe' -and $_.CommandLine -like '*\EvolutionWin\*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
