@echo off
:: build.bat — Compilar ZAR-SHELL en Windows sin Makefile
:: Uso: build.bat [ruta_a_g++]
:: Ejemplo: build.bat C:\Users\%USERNAME%\AppData\Local\alire\cache\msys64\ucrt64\bin

setlocal

:: Si se pasó una ruta, agregarla al PATH temporal
if not "%~1"=="" (
    set "PATH=%~1;%PATH%"
)

:: Compilar
echo Compilando ZAR-SHELL...
g++ -std=c++17 -O2 -static src\zar_shell.cpp -o zar_shell.exe

if %ERRORLEVEL% == 0 (
    echo.
    echo   [OK] Compilado correctamente: zar_shell.exe
    echo   Ejecuta con: zar_shell.exe
    echo.
) else (
    echo.
    echo   [ERROR] La compilacion fallo.
    echo   Asegurate de tener g++ en tu PATH.
    echo   Puedes instalarlo con MSYS2: pacman -S mingw-w64-ucrt-x86_64-gcc
    echo.
)

endlocal
