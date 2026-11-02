@echo off
rem Environnement commun pour les scripts de compilation MSVC (appelé par build-*.bat).
rem
rem Tous les chemins sont déduits de l'emplacement de ce script (dossier tools\) :
rem   <workspace>\evolution               sources d'Evolution (GTK)
rem   <workspace>\evolution-data-server   sources d'EDS
rem   <workspace>\evolution-ews           sources d'evolution-ews (Exchange / Microsoft 365)
rem   <workspace>\winui                  client natif WinUI 3 (evo-core + EvolutionWin)
rem   <workspace>\tools                   scripts et outils de portage MSVC
rem   <workspace>\deps                    sources tierces (NSS, NSPR, libical, SDK WebView2)
rem   <workspace>\gtk-build               pile GTK / préfixe d'installation commun
rem   <workspace>\build                   exécutables finaux et dossiers intermédiaires (build\intermediate)

if not defined EVO_WORKSPACE for %%I in ("%~dp0..") do set "EVO_WORKSPACE=%%~fI"
if not defined EVO_SRC set "EVO_SRC=%EVO_WORKSPACE%\evolution"
if not defined EDS_SRC set "EDS_SRC=%EVO_WORKSPACE%\evolution-data-server"
if not defined EWS_SRC set "EWS_SRC=%EVO_WORKSPACE%\evolution-ews"
if not defined WINUI_SRC set "WINUI_SRC=%EVO_WORKSPACE%\winui"
if not defined DEPS set "DEPS=%EVO_WORKSPACE%\deps"
if not defined BUILD_DIR set "BUILD_DIR=%EVO_WORKSPACE%\build"
if not defined BUILD_INTERMEDIATE set "BUILD_INTERMEDIATE=%BUILD_DIR%\intermediate"

rem Worktree Git secondaire (branches de travail en parallèle) : la pile GTK et
rem les paquets NuGet, volumineux et hors Git, sont repris du dépôt principal
set "EVO_MAIN_WORKSPACE=%EVO_WORKSPACE%"
for /f "usebackq delims=" %%I in (`git -C "%EVO_WORKSPACE%" rev-parse --path-format^=absolute --git-common-dir 2^>nul`) do for %%J in ("%%I\..") do set "EVO_MAIN_WORKSPACE=%%~fJ"

if not defined GTK_BUILD (
	if exist "%EVO_WORKSPACE%\gtk-build\gtk\x64\release" (
		set "GTK_BUILD=%EVO_WORKSPACE%\gtk-build"
	) else if exist "%EVO_MAIN_WORKSPACE%\gtk-build\gtk\x64\release" (
		set "GTK_BUILD=%EVO_MAIN_WORKSPACE%\gtk-build"
	) else (
		set "GTK_BUILD=C:\gtk-build"
	)
)
if not defined WINUI_PACKAGES (
	if exist "%WINUI_SRC%\packages" (
		set "WINUI_PACKAGES=%WINUI_SRC%\packages"
	) else if exist "%EVO_MAIN_WORKSPACE%\winui\packages" (
		set "WINUI_PACKAGES=%EVO_MAIN_WORKSPACE%\winui\packages"
	) else (
		set "WINUI_PACKAGES=%WINUI_SRC%\packages"
	)
)
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "PREFIX=%GTK_BUILD%\gtk\x64\release"
set "MSVC_SCRIPTS=%~dp0"

rem Perl natif téléchargé par gvsbuild (requis par NSS et libical)
if not defined PERL set "PERL=%GTK_BUILD%\tools\perl-5.20.0\x64\bin\perl.exe"

rem cmd.exe doit pouvoir exécuter les scripts du répertoire courant
set NoDefaultCurrentDirectoryInExePath=

rem =========================================================================
rem Localisation de Visual Studio (2026, 2022 ou personnalisé via VS_VERSION / VS_INSTALL)
rem =========================================================================
rem Variables configurables par l'utilisateur :
rem   VS_VERSION : "2026" / "18" (VS 2026), "2022" / "17" (VS 2022), "latest" ou non défini (plus récente)
rem   VS_INSTALL : chemin direct d'installation (ex: C:\Program Files\Microsoft Visual Studio\18\Community)

if defined VS_VERSION (
	for /f "tokens=1" %%V in ("%VS_VERSION%") do set "VS_VERSION=%%V"
)

if not defined VS_VERSION if defined VS_INSTALL (
	if exist "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" goto :vcvars_found
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
		set "VS_INSTALL=C:\Program Files\Microsoft Visual Studio\18\Community"
		goto :vcvars_found
	)
	if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
		set "VS_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\Community"
		goto :vcvars_found
	)
)

set "VS_RANGE_ARG="
if "%VS_VERSION:~0,4%"=="2026" set "VS_RANGE_ARG=-version [18.0^,19.0^)"
if "%VS_VERSION:~0,2%"=="18" set "VS_RANGE_ARG=-version [18.0^,19.0^)"
if "%VS_VERSION:~0,4%"=="2022" set "VS_RANGE_ARG=-version [17.0^,18.0^)"
if "%VS_VERSION:~0,2%"=="17" set "VS_RANGE_ARG=-version [17.0^,18.0^)"

set "VS_INSTALL="
if not defined VS_RANGE_ARG (
	rem Par défaut : la version la plus récente installée (VS 2026 en priorité si présent, sinon VS 2022)
	for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VS_INSTALL=%%I"
	if not defined VS_INSTALL (
		for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x64.x86 -property installationPath 2^>nul`) do set "VS_INSTALL=%%I"
	)
) else (
	for /f "usebackq delims=" %%I in (`"%VSWHERE%" %VS_RANGE_ARG% -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VS_INSTALL=%%I"
	if not defined VS_INSTALL (
		for /f "usebackq delims=" %%I in (`"%VSWHERE%" %VS_RANGE_ARG% -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x64.x86 -property installationPath 2^>nul`) do set "VS_INSTALL=%%I"
	)
)

if not defined VS_INSTALL (
	if defined VS_VERSION (
		echo Visual Studio version %VS_VERSION% avec les outils C++ est introuvable >&2
	) else (
		echo Visual Studio 2026/2022 avec les outils C++ est introuvable >&2
	)
	exit /b 1
)

:vcvars_found
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "PATH=%PREFIX%\bin;%APPDATA%\Python\Python313\Scripts;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
rem cl.exe et link.exe lisent INCLUDE et LIB lors des tests de configuration CMake
set "INCLUDE=%PREFIX%\include;%INCLUDE%"
set "LIB=%PREFIX%\lib;%LIB%"
set "PKG_CONFIG=%PREFIX%\bin\pkgconf.exe"
set "PKG_CONFIG_PATH=%PREFIX%\lib\pkgconfig"
exit /b 0
