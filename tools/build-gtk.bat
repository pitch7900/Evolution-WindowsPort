@echo off
rem Builds the GTK3 stack and the other gvsbuild-provided dependencies of
rem evolution-data-server and Evolution with MSVC (x64, release).
rem The result is installed in %GTK_BUILD%\gtk\x64\release (see env.bat).
rem
rem Also used to recreate the stack when <workspace>\gtk-build is missing (the
rem folder is too large for Git): gvsbuild, meson, ninja and gyp-next are
rem installed with pip if needed. Unless GTK_BUILD is set, the stack is built in
rem <workspace>\gtk-build.
rem
rem Usage: tools\build-gtk.bat [--clean]
rem   (no option)  incremental: gvsbuild only rebuilds what is missing
rem   --clean      deletes %GTK_BUILD% first (full rebuild, 1-2 h), after confirmation

setlocal enabledelayedexpansion
for %%I in ("%~dp0..") do set "EVO_WORKSPACE=%%~fI"
if not defined GTK_BUILD set "GTK_BUILD=%EVO_WORKSPACE%\gtk-build"

if /i "%~1"=="--clean" (
	if exist "%GTK_BUILD%" (
		echo This will DELETE %GTK_BUILD% and rebuild everything.
		set /p "CONFIRM=Continue? (y/N) "
		if /i not "!CONFIRM!"=="y" (
			echo Cancelled.
			exit /b 1
		)
		rmdir /s /q "%GTK_BUILD%" || exit /b 1
	)
)

call "%~dp0env.bat" || exit /b 1

if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
	echo MSYS2 not found in %MSYS2_ROOT%: install it ^(winget install MSYS2.MSYS2^) or set MSYS2_ROOT >&2
	exit /b 1
)

where python >nul 2>&1 || (
	echo Python not found in PATH >&2
	exit /b 1
)

where gvsbuild >nul 2>&1 || (
	echo Installing gvsbuild, meson, ninja and gyp-next...
	python -m pip install --user --upgrade gvsbuild meson ninja gyp-next || exit /b 1
)
where gvsbuild >nul 2>&1 || (
	echo gvsbuild installed but not in PATH: check %%APPDATA%%\Python\Python3xx\Scripts >&2
	exit /b 1
)

echo GTK stack: %GTK_BUILD%

gvsbuild build --fast-build ^
	--build-dir "%GTK_BUILD%" ^
	--msys-dir "%MSYS2_ROOT%" ^
	--vs-ver vs2022 ^
	--configuration release ^
	gtk3 libsoup3 glib-networking libxml2 sqlite enchant json-glib ^
	gsettings-desktop-schemas adwaita-icon-theme hicolor-icon-theme ^
	librsvg pkgconf icu libarchive openssl || exit /b 1

rem gvsbuild's zlib import library is zlib1.lib; EDS/Evolution link with -lz
copy /y "%PREFIX%\lib\zlib1.lib" "%PREFIX%\lib\z.lib" >nul || exit /b 1

rem These gvsbuild projects ship no pkg-config files: install ours
call "%~dp0install-pc.bat" icu-uc icu-i18n sqlite3 enchant-2 gsettings-desktop-schemas shared-mime-info || exit /b 1

echo.
echo GTK stack ready in %PREFIX%
endlocal
