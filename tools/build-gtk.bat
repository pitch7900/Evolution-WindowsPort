@echo off
rem Builds the GTK3 stack and the other gvsbuild-provided dependencies of
rem evolution-data-server and Evolution with MSVC (x64, release).
rem The result is installed in %GTK_BUILD%\gtk\x64\release (see env.bat).

setlocal
call "%~dp0env.bat" || exit /b 1

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"

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
endlocal
