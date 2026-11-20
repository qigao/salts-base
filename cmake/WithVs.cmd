@echo off
setlocal
if not defined SALTS_VS_ARCH set "SALTS_VS_ARCH=x64"
call "%VSDEVCMD_BAT%" -arch=%SALTS_VS_ARCH% -host_arch=%SALTS_VS_ARCH% >nul
if errorlevel 1 exit /b %errorlevel%
%*
exit /b %errorlevel%
