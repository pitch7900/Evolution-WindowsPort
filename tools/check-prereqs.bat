@echo off
rem Checks the build prerequisites; pass -Install to download/install what is missing.
rem   check-prereqs.bat
rem   check-prereqs.bat -Install
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-prereqs.ps1" %*
exit /b %ERRORLEVEL%
