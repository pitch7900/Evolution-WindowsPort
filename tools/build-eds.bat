@echo off
rem Compile evolution-data-server avec MSVC (x64, release) dans le préfixe commun.
rem Les chemins proviennent de env.bat (sources dans %EDS_SRC%).

setlocal
call "%~dp0env.bat" || exit /b 1

if not exist "%EDS_SRC%\CMakeLists.txt" (
	echo Sources evolution-data-server introuvables dans "%EDS_SRC%", voir WINDOWS-PORT.md >&2
	exit /b 1
)

set "BUILD=%BUILD_INTERMEDIATE%\eds"

cmake -S "%EDS_SRC%" -B "%BUILD%" -G Ninja ^
	-DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
	-DCMAKE_PREFIX_PATH="%PREFIX%" ^
	-DCMAKE_PROJECT_INCLUDE="%MSVC_SCRIPTS%cmake\msvc-compat.cmake" ^
	-DENABLE_GTK=ON ^
	-DENABLE_GTK4=OFF ^
	-DENABLE_OAUTH2_WEBKITGTK=OFF ^
	-DENABLE_OAUTH2_WEBKITGTK4=OFF ^
	-DENABLE_GOA=OFF ^
	-DENABLE_WEATHER=OFF ^
	-DENABLE_CANBERRA=OFF ^
	-DENABLE_EXAMPLES=OFF ^
	-DENABLE_TESTS=OFF ^
	-DENABLE_INSTALLED_TESTS=OFF ^
	-DENABLE_INTROSPECTION=OFF ^
	-DENABLE_VALA_BINDINGS=OFF ^
	-DENABLE_GTK_DOC=OFF ^
	-DENABLE_GI_DOCGEN=OFF ^
	-DWITH_LIBDB=OFF ^
	-DWITH_OPENLDAP=OFF ^
	-DWITH_KRB5=OFF ^
	-DWITH_PHONENUMBER=OFF ^
	-DENABLE_SMIME=ON || exit /b 1

cmake --build "%BUILD%" -- -k 0 || exit /b 1
cmake --install "%BUILD%" || exit /b 1

if exist "%PREFIX%\lib\*.dll" move /y "%PREFIX%\lib\*.dll" "%PREFIX%\bin\" >nul
glib-compile-schemas "%PREFIX%\share\glib-2.0\schemas" || exit /b 1

if not exist "%BUILD_DIR%\libexec" mkdir "%BUILD_DIR%\libexec"
copy /y "%PREFIX%\libexec\*.exe" "%BUILD_DIR%\libexec\" >nul
endlocal
