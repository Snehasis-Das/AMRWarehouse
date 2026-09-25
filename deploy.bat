@echo off
set "ROOT=%~dp0"
set "EDITOR=%ROOT%warehouse-editor\dist"
set "SIMULATOR=%ROOT%amr-simulator\dist"
set "FINAL=%ROOT%dist"

if not exist "%EDITOR%" (
    echo ERROR: Editor dist not found.
    exit /b 1
)

if not exist "%SIMULATOR%" (
    echo ERROR: Simulator dist not found.
    exit /b 1
)

if exist "%FINAL%" rmdir /s /q "%FINAL%"
mkdir "%FINAL%"

xcopy "%EDITOR%\*" "%FINAL%\" /E /I /Y /H >nul
xcopy "%SIMULATOR%\*" "%FINAL%\" /E /I /Y /H >nul