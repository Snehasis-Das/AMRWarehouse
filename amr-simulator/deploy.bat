@echo off

set PROJECT_DIR=%~dp0
set BUILD_DIR=%PROJECT_DIR%build_release\Release
set DIST_DIR=%PROJECT_DIR%dist
set SFML_DIR=C:\SFML-3.0.2-windows-vc17-64-bit\SFML-3.0.2

if exist "%DIST_DIR%" rmdir /S /Q "%DIST_DIR%"

mkdir "%DIST_DIR%"

copy /Y "%BUILD_DIR%\AMRSimulator.exe" "%DIST_DIR%\" >nul

copy /Y "%SFML_DIR%\bin\sfml-graphics-3.dll" "%DIST_DIR%\" >nul
copy /Y "%SFML_DIR%\bin\sfml-window-3.dll" "%DIST_DIR%\" >nul
copy /Y "%SFML_DIR%\bin\sfml-system-3.dll" "%DIST_DIR%\" >nul

xcopy /E /I /Y "%PROJECT_DIR%resources" "%DIST_DIR%\resources" >nul