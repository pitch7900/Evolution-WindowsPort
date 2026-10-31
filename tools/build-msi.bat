@echo off
rem Construit l'installeur MSI d'EvolutionWin (phase 5) dans .\dist\.
rem Requiert les builds précédents (EDS, evo-core, nuget restore de l'app)
rem et WiX Toolset v5 (dotnet tool install --global wix --version 5.0.2).
rem
rem   tools\build-msi.bat [-Version x.y.z] [-SkipAppBuild]
rem
rem Version par défaut : fichier VERSION à la racine (voir CHANGELOG.md).
rem
rem Signature optionnelle : SIGN_CERT (fichier .pfx), SIGN_CERT_PASSWORD,
rem SIGN_TIMESTAMP_URL. Sans SIGN_CERT, le MSI n'est pas signé.

setlocal
call "%~dp0env.bat" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-msi.ps1" %* || exit /b 1
endlocal
