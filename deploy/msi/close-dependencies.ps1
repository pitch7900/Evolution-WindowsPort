# Fermeture des dependances d'EvolutionWin avant mise a jour / desinstallation.
# Source lisible du script embarque (en base64, -EncodedCommand) dans tools\msi\Evolution.wxs
# par tools\build-msi.ps1 : le script est re-embarque a chaque construction du MSI.
# Execute dans TOUTES les sessions (serveur multi-sessions, session deconnectee) :
# demande de fermeture propre, 5 s de delai, puis arret force.
$ErrorActionPreference = 'SilentlyContinue'
$install = $env:ProgramFiles + '\Evolution\'
# Tout processus lance depuis le dossier d'installation (bin\ et libexec\ : services EDS, camel-index-control-1.2,
# gspawn-win64-helper, gdbus, ...) plus EvolutionWin lui-meme, quel que soit son chemin. Les processus d'autres
# applications GLib (hors dossier d'installation) sont epargnes.
$cibles = Get-Process | Where-Object { $_.Path -like ($install + '*') -or $_.ProcessName -eq 'EvolutionWin' }
$cibles | ForEach-Object { $null = $_.CloseMainWindow() }
if ($cibles) { Start-Sleep -Seconds 5 }
$cibles | Stop-Process -Force
# WebView2 est partage avec d'autres applications : seuls les processus dont la ligne de commande vise le dossier
# de donnees d'EvolutionWin sont arretes (%LOCALAPPDATA%\EvolutionWin\WebView2, ou, avant la 1.3.6, le dossier
# "EvolutionWin.exe.WebView2" cree a cote de l'executable)
Get-CimInstance Win32_Process |
    Where-Object { $_.Name -eq 'msedgewebview2.exe' -and ($_.CommandLine -like '*\EvolutionWin\*' -or $_.CommandLine -like '*EvolutionWin.exe.WebView2*') } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
