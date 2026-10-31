@echo off
rem Starts EvolutionWin with diagnostics: engine debug messages and the IMAP
rem protocol trace (passwords are masked by Camel), written to
rem %LOCALAPPDATA%\EvolutionWin\evo-core.log and imap-trace.log.

setlocal
set EVO_CORE_DEBUG=1
set CAMEL_DEBUG=imapx
call "%~dp0run-winui.bat"
endlocal
