@echo off
rem Builds NSPR + NSS with MSVC (x64, release) using the NSS gyp/ninja build.
rem MSYS2 bash is only used to run build.sh; the compiler is cl.exe.
rem Paths come from env.bat (sources in %DEPS%\nss and %DEPS%\nspr).

setlocal
call "%~dp0env.bat" || exit /b 1

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set MSYS2_PATH_TYPE=inherit
set CHERE_INVOKING=1

"%MSYS2_ROOT%\usr\bin\bash.exe" -l "%~dp0build-nss.sh" "%DEPS%" || exit /b 1

rem Install headers, import libraries and DLLs into the common prefix
set "DIST=%DEPS%\dist"
set "OBJ=%DIST%\Release"
xcopy /y /e /i "%DIST%\public\nss" "%PREFIX%\include\nss3" >nul || exit /b 1
xcopy /y /e /i "%OBJ%\include\nspr" "%PREFIX%\include\nspr" >nul || exit /b 1
copy /y "%OBJ%\lib\*.dll" "%PREFIX%\bin\" >nul
copy /y "%OBJ%\lib\*.lib" "%PREFIX%\lib\" >nul

rem NSS names its import libraries foo.dll.lib; -lfoo needs foo.lib
for %%L in (nss3 nssutil3 smime3 ssl3) do copy /y "%PREFIX%\lib\%%L.dll.lib" "%PREFIX%\lib\%%L.lib" >nul

rem NSS/NSPR ship no pkg-config files on Windows: install ours
call "%~dp0install-pc.bat" nss nspr || exit /b 1
endlocal
