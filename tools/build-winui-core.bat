@echo off
rem Compile evo-core (moteur C du client natif WinUI, winui\core)
rem avec MSVC (x64, release) dans le préfixe commun et copie les binaires dans build\.

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
cmake --install "%BUILD%" || exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
copy /y "%PREFIX%\bin\evo-core.dll" "%BUILD_DIR%\" >nul
copy /y "%PREFIX%\bin\evo-core-test.exe" "%BUILD_DIR%\" >nul
endlocal
