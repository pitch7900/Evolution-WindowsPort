@echo off
rem Compile evolution-ews (Exchange Web Services et Microsoft 365 / Graph)
rem avec MSVC (x64, release) dans le préfixe commun. Requiert EDS
rem (build-eds.bat). Seules les parties EDS sont compilées (fournisseurs
rem Camel, backends carnet/agenda, modules du registre) : WITH_EVOLUTION=OFF,
rem les pages de configuration de l'interface GTK ne servent pas à EvolutionWin.

setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD=%BUILD_INTERMEDIATE%\ews"

cmake -S "%EWS_SRC%" -B "%BUILD%" -G Ninja ^
	-DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
	-DCMAKE_PREFIX_PATH="%PREFIX%" ^
	-DCMAKE_PROJECT_INCLUDE="%MSVC_SCRIPTS%cmake\msvc-compat.cmake" ^
	-DWITH_EVOLUTION=OFF ^
	-DWITH_MSPACK=OFF ^
	-DENABLE_TESTS=OFF || exit /b 1

cmake --build "%BUILD%" -- -k 0 || exit /b 1
cmake --install "%BUILD%" || exit /b 1

rem bibliothèques partagées installées dans lib\ à la manière Unix
if exist "%PREFIX%\lib\*.dll" move /y "%PREFIX%\lib\*.dll" "%PREFIX%\bin\" >nul
glib-compile-schemas "%PREFIX%\share\glib-2.0\schemas" || exit /b 1
endlocal
