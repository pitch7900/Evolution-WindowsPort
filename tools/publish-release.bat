@echo off
rem Publie la version courante (fichier VERSION) dans les Releases GitHub :
rem étiquette vX.Y.Z, notes du CHANGELOG, MSI de dist\ et son empreinte .sha256.
rem À lancer après tools\build-msi.bat, une fois la section du CHANGELOG datée.
rem
rem   tools\publish-release.bat [-Version x.y.z] [-PreRelease] [-DryRun]
rem
rem Jeton GitHub : GH_TOKEN / GITHUB_TOKEN, sinon identifiants de Git.

setlocal
call "%~dp0env.bat" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish-release.ps1" %* || exit /b 1
endlocal
