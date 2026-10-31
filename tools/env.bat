@echo off
rem Environnement commun pour les scripts de compilation MSVC (appelé par build-*.bat).
rem
rem Tous les chemins sont déduits de l'emplacement de ce script (dossier tools\) :
rem   <workspace>\evolution               sources d'Evolution (GTK)
rem   <workspace>\evolution-data-server   sources d'EDS
rem   <workspace>\winui                   client natif WinUI 3 (evo-core + EvolutionWin)
rem   <workspace>\tools                   scripts et outils de portage MSVC
rem   <workspace>\deps                    sources tierces (NSS, NSPR, libical, SDK WebView2)
rem   <workspace>\gtk-build               pile GTK / préfixe d'installation commun
rem   <workspace>\build                   exécutables finaux et dossiers intermédiaires (build\intermediate)

if not defined EVO_WORKSPACE for %%I in ("%~dp0..") do set "EVO_WORKSPACE=%%~fI"
if not defined EVO_SRC set "EVO_SRC=%EVO_WORKSPACE%\evolution"
if not defined EDS_SRC set "EDS_SRC=%EVO_WORKSPACE%\evolution-data-server"
if not defined WINUI_SRC set "WINUI_SRC=%EVO_WORKSPACE%\winui"
if not defined DEPS set "DEPS=%EVO_WORKSPACE%\deps"
if not defined BUILD_DIR set "BUILD_DIR=%EVO_WORKSPACE%\build"
if not defined BUILD_INTERMEDIATE set "BUILD_INTERMEDIATE=%BUILD_DIR%\intermediate"
if not defined GTK_BUILD (
	if exist "%EVO_WORKSPACE%\gtk-build\gtk\x64\release" (
		set "GTK_BUILD=%EVO_WORKSPACE%\gtk-build"
	) else (
		set "GTK_BUILD=C:\gtk-build"
	)
)
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "PREFIX=%GTK_BUILD%\gtk\x64\release"
set "MSVC_SCRIPTS=%~dp0"

rem Perl natif téléchargé par gvsbuild (requis par NSS et libical)
if not defined PERL set "PERL=%GTK_BUILD%\tools\perl-5.20.0\x64\bin\perl.exe"

rem cmd.exe doit pouvoir exécuter les scripts du répertoire courant
set NoDefaultCurrentDirectoryInExePath=

rem Localisation automatique de Visual Studio 2022 avec vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x64.x86 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
	for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
)
if not defined VS_INSTALL (
	echo Visual Studio 2022 avec les outils C++ est introuvable >&2
	exit /b 1
)
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "PATH=%PREFIX%\bin;%APPDATA%\Python\Python313\Scripts;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
rem cl.exe et link.exe lisent INCLUDE et LIB lors des tests de configuration CMake
set "INCLUDE=%PREFIX%\include;%INCLUDE%"
set "LIB=%PREFIX%\lib;%LIB%"
set "PKG_CONFIG=%PREFIX%\bin\pkgconf.exe"
set "PKG_CONFIG_PATH=%PREFIX%\lib\pkgconfig"
exit /b 0
