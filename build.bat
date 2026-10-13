@echo off
rem Double-click here to rebuild, validate and install Heft.
rem From a terminal you can call build.ps1 directly, with options, for example:
rem   powershell -ExecutionPolicy Bypass -File build.ps1 -Strictness 5 -Measure
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set "EXITCODE=%ERRORLEVEL%"
echo.
pause
exit /b %EXITCODE%
