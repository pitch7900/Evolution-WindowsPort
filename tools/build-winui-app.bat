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

if not exist "%WINUI_SRC%\packages\Microsoft.WindowsAppSDK.1.7.260224002" (
	"%NUGET%" restore "%APP%\packages.config" -PackagesDirectory "%WINUI_SRC%\packages" -NonInteractive || exit /b 1
)

rem OutDir/IntDir se terminent par "\" : non entourés de guillemets pour MSBuild
msbuild "%APP%\EvolutionWin.vcxproj" -nologo -m -v:minimal ^
	-p:Configuration=Release -p:Platform=x64 ^
	-p:GtkPrefix="%PREFIX%" ^
	-p:SDKReferenceDirectoryRoot="%ProgramFiles(x86)%\Microsoft SDKs" ^
	-p:OutDir=%OUT% -p:IntDir=%BUILD_INTERMEDIATE%\winui-app-obj\ || exit /b 1

echo EvolutionWin compilé dans %OUT%
endlocal
