@echo off
rem Compile evo-core (moteur C du client natif WinUI, winui\core) avec MSVC
rem (x64, release) dans build\intermediate\evo-core et copie la DLL et
rem evo-core-test.exe dans build\. Rien n'est installé dans le préfixe commun :
rem plusieurs copies de travail (worktrees) peuvent compiler en même temps,
rem chacune liant EvolutionWin à son propre evo-core (EvoCoreLibDir).

setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD=%BUILD_INTERMEDIATE%\evo-core"

cmake -S "%WINUI_SRC%\core" -B "%BUILD%" -G Ninja ^
	-DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
	-DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
	-DCMAKE_PREFIX_PATH="%PREFIX%" ^
	-DCMAKE_PROJECT_INCLUDE="%MSVC_SCRIPTS%cmake\msvc-compat.cmake" || exit /b 1

cmake --build "%BUILD%" || exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if exist "%BUILD_DIR%\evo-core.dll" move /y "%BUILD_DIR%\evo-core.dll" "%BUILD_DIR%\evo-core.dll.old" >nul 2>&1
if exist "%BUILD_DIR%\evo-core.dll.old" del /f /q "%BUILD_DIR%\evo-core.dll.old" >nul 2>&1
copy /y "%BUILD%\evo-core.dll" "%BUILD_DIR%\" >nul
copy /y "%BUILD%\evo-core-test.exe" "%BUILD_DIR%\" >nul
endlocal
