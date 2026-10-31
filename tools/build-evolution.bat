@echo off
rem Compile Evolution (version GTK transitoire) avec MSVC (x64, release) dans le préfixe commun.
rem Les chemins proviennent de env.bat (sources dans %EVO_SRC%).

setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD=%BUILD_INTERMEDIATE%\evolution"

cmake -S "%EVO_SRC%" -B "%BUILD%" -G Ninja ^
	-DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
	-DCMAKE_PREFIX_PATH="%PREFIX%" ^
	-DCMAKE_PROJECT_INCLUDE="%MSVC_SCRIPTS%cmake\msvc-compat.cmake" ^
	-DENABLE_GNOME_DESKTOP=OFF ^
	-DENABLE_CANBERRA=OFF ^
	-DENABLE_AUTOAR=OFF ^
	-DWITH_HELP=OFF ^
	-DENABLE_YTNEF=OFF ^
	-DENABLE_LIBNOTIFY=OFF ^
	-DENABLE_GSPELL=OFF ^
	-DENABLE_TEXT_HIGHLIGHT=OFF ^
	-DENABLE_WEATHER=OFF ^
	-DENABLE_PST_IMPORT=OFF ^
	-DENABLE_MARKDOWN=OFF ^
	-DWITH_OPENLDAP=OFF ^
	-DWITH_BOGOFILTER=OFF ^
	-DWITH_SPAMASSASSIN=OFF ^
	-DENABLE_ALARM_NOTIFY_MODULE=ON || exit /b 1

cmake --build "%BUILD%" -- -k 0 || exit /b 1
cmake --install "%BUILD%" || exit /b 1

if exist "%PREFIX%\lib\*.dll" move /y "%PREFIX%\lib\*.dll" "%PREFIX%\bin\" >nul
glib-compile-schemas "%PREFIX%\share\glib-2.0\schemas" || exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
copy /y "%PREFIX%\bin\evolution.exe" "%BUILD_DIR%\" >nul
copy /y "%PREFIX%\bin\evolution-*.dll" "%BUILD_DIR%\" >nul
endlocal
