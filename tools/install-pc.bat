@echo off
rem Installs pkg-config files for libraries that ship none on Windows.
rem Usage (after env.bat): install-pc.bat <name> [<name> ...]
rem Each pkgconfig\<name>.pc.in is copied to %PREFIX%\lib\pkgconfig\<name>.pc
rem with @PREFIX@ replaced by the prefix (forward slashes).

setlocal
if not defined PREFIX (
	echo install-pc.bat: PREFIX is not set, call env.bat first >&2
	exit /b 1
)

rem saved before the loop: shift also shifts %0
set "TEMPLATES=%~dp0pkgconfig"

:next
if "%~1"=="" exit /b 0
powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; (Get-Content '%TEMPLATES%\%~1.pc.in') -replace '@PREFIX@', ('%PREFIX%' -replace '\\','/') | Set-Content -Encoding ascii '%PREFIX%\lib\pkgconfig\%~1.pc'" || exit /b 1
shift
goto next
