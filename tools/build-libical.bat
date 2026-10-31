@echo off
rem Compile libical + libical-glib avec MSVC (x64, release) dans le préfixe commun.
rem Les chemins proviennent de env.bat (sources dans %DEPS%\libical).

setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD=%BUILD_INTERMEDIATE%\libical"

rem Windows n'a pas de base zoneinfo système : utilisation des fuseaux intégrés.
cmake -S "%DEPS%\libical" -B "%BUILD%" -G Ninja ^
	-DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
	-DCMAKE_PREFIX_PATH="%PREFIX%" ^
	-DCMAKE_PROJECT_INCLUDE="%MSVC_SCRIPTS%cmake\msvc-compat.cmake" ^
	-DPERL_EXECUTABLE="%PERL%" ^
	-DUSE_BUILTIN_TZDATA=ON ^
	-DICAL_GLIB=ON ^
	-DICAL_GLIB_VAPI=OFF ^
	-DGOBJECT_INTROSPECTION=OFF ^
	-DICAL_BUILD_DOCS=OFF ^
	-DLIBICAL_BUILD_TESTING=OFF ^
	-DWITH_CXX_BINDINGS=OFF ^
	-DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON ^
	-DCMAKE_DISABLE_FIND_PACKAGE_BDB=ON || exit /b 1

cmake --build "%BUILD%" || exit /b 1
cmake --install "%BUILD%" || exit /b 1

rem libical 3.0 n'installe pas libicalvcal.pc, requis par Evolution
call "%~dp0install-pc.bat" libicalvcal || exit /b 1
endlocal
