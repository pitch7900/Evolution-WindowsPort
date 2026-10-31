@echo off
rem Démarre EvolutionWin depuis le dossier .\build, avec les binaires MSVC
rem d'evo-core, evolution-data-server et GLib dans le PATH.

setlocal
for %%I in ("%~dp0..") do set "EVO_WORKSPACE=%%~fI"
if not defined BUILD_DIR set "BUILD_DIR=%EVO_WORKSPACE%\build"
if not defined GTK_BUILD (
	if exist "%EVO_WORKSPACE%\gtk-build\gtk\x64\release" (
		set "GTK_BUILD=%EVO_WORKSPACE%\gtk-build"
	) else (
		set "GTK_BUILD=C:\gtk-build"
	)
)

set "PATH=%GTK_BUILD%\gtk\x64\release\bin;%PATH%"
start "" "%BUILD_DIR%\EvolutionWin.exe"
endlocal
