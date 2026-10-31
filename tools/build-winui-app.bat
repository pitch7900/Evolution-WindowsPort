@echo off
rem Compile EvolutionWin, le client natif WinUI 3 (winui\app), avec MSBuild.
rem Requiert evo-core (build-winui-core.bat). Sortie dans %BUILD_DIR%\ (.\build\).

setlocal
call "%~dp0env.bat" || exit /b 1

set "NUGET=%MSVC_SCRIPTS%tools\nuget.exe"
if not exist "%NUGET%" (
	echo nuget.exe introuvable dans %MSVC_SCRIPTS%tools, lancez check-prereqs.bat -Install >&2
	exit /b 1
)

set "APP=%WINUI_SRC%\app"
set "OUT=%BUILD_DIR%\"

if not exist "%WINUI_PACKAGES%\Microsoft.WindowsAppSDK.1.7.260224002" (
	"%NUGET%" restore "%APP%\packages.config" -PackagesDirectory "%WINUI_PACKAGES%" -NonInteractive || exit /b 1
)

rem OutDir/IntDir/PackagesDir/EvoCoreLibDir se terminent par "\" : non entourés de guillemets pour MSBuild
rem (paquets NuGet partagés entre worktrees, evo-core de cette copie : build-winui-core.bat)
if exist "%BUILD_DIR%\EvolutionWin.exe" move /y "%BUILD_DIR%\EvolutionWin.exe" "%BUILD_DIR%\EvolutionWin.exe.old" >nul 2>&1
if exist "%BUILD_DIR%\EvolutionWin.exe.old" del /f /q "%BUILD_DIR%\EvolutionWin.exe.old" >nul 2>&1
msbuild "%APP%\EvolutionWin.vcxproj" -nologo -m -v:minimal ^
	-p:Configuration=Release -p:Platform=x64 ^
	-p:GtkPrefix="%PREFIX%" ^
	-p:PackagesDir=%WINUI_PACKAGES%\ ^
	-p:EvoCoreLibDir=%BUILD_INTERMEDIATE%\evo-core\ ^
	-p:SDKReferenceDirectoryRoot="%ProgramFiles(x86)%\Microsoft SDKs" ^
	-p:OutDir=%OUT% -p:IntDir=%BUILD_INTERMEDIATE%\winui-app-obj\ || exit /b 1

echo EvolutionWin compilé dans %OUT%
endlocal
