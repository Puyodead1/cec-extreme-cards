@echo off
rem Start build @B@. The data tree's directory links are junctions with absolute targets, so they are
rem (re)made here on every start, which also keeps them right after the folder is moved.
setlocal EnableDelayedExpansion
set "V=%~dp0vend-@B@"
for /f "usebackq tokens=1,2 delims=	" %%a in ("%V%\.junctions") do (
    set "L=%%a" & set "T=%%b"
    set "L=!L:/=\!" & set "T=!T:/=\!"
    if exist "%V%\!L!\" rmdir "%V%\!L!" 2>nul
    if not exist "%V%\!L!\" mklink /J "%V%\!L!" "%V%\!T!" >nul
)
rem the engine counts failed starts and refuses to run after two ("Too many reboots"); closing the window counts as one
del /f /q "%V%\exiterr.dat" 2>nul
start "" "%~dp0vjmnupns-@B@.exe" %*
