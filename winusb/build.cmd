@echo off
setlocal

rem build.ps1 is the real build entry point.  This wrapper keeps the console
rem open after a double-click so that dependency and compiler errors remain
rem visible.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
set "BUILD_RESULT=%ERRORLEVEL%"

echo.
if not "%BUILD_RESULT%"=="0" (
    echo Build failed. Review the message above.
) else (
    echo Build completed successfully.
)
pause
exit /b %BUILD_RESULT%
