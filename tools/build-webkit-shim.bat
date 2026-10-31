@echo off
rem Compile le shim de compatibilité WebKitGTK (webkit2gtk-4.1.dll sur WebView2)
rem avec MSVC (x64, release) dans le préfixe commun.

setlocal
call "%~dp0env.bat" || exit /b 1

set "BUILD=%BUILD_INTERMEDIATE%\webkit-shim"

cmake -S "%MSVC_SCRIPTS%webkit-shim" -B "%BUILD%" -G Ninja ^
	-DCMAKE_BUILD_TYPE=Release ^
	-DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
	-DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
	-DCMAKE_PREFIX_PATH="%PREFIX%" ^
	-DCMAKE_PROJECT_INCLUDE="%MSVC_SCRIPTS%cmake\msvc-compat.cmake" ^
	-DWEBVIEW2_SDK="%DEPS%\webview2\sdk\build\native" || exit /b 1

cmake --build "%BUILD%" || exit /b 1
cmake --install "%BUILD%" || exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
copy /y "%PREFIX%\bin\webkit2gtk-4.1.dll" "%BUILD_DIR%\" >nul
endlocal
