@echo off
chcp 65001 >nul

echo ========================================
echo    Windows Driver Build Script
echo ========================================

:: Устанавливаем окружение для x86_x64 Cross Tools
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsx86_amd64.bat"

echo.
echo Building with x86_x64 Cross Tools...
echo Configuration: Debug
echo Platform: x64
echo PlatformToolset: WindowsKernelModeDriver10.0
echo.

:: Проверка наличия файла решения
if not exist "regfltr.sln" (
    echo ERROR: regfltr.sln not found!
    echo Current directory: %CD%
    pause
    exit /b 1
)

:: Очистка папки bin
if exist "bin" (
    echo Cleaning bin folder...
    rmdir /s /q "bin"
)
mkdir "bin" >nul 2>&1

:: Сборка проекта
msbuild regfltr.sln ^
    /p:Configuration=Debug ^
    /p:Platform=x64 ^
    /p:PlatformToolset=WindowsKernelModeDriver10.0 ^
    /verbosity:minimal

if %errorlevel% neq 0 (
    echo.
    echo ========================================
    echo    Build FAILED!
    echo ========================================
    echo.
    pause
    exit /b %errorlevel%
)

echo.
echo ========================================
echo    Build SUCCESSFUL!
echo ========================================
echo.

:: Копирование файлов в bin
echo Copying files to bin folder...

if exist "exe\x64\Debug\regctrl.exe" (
    copy "exe\x64\Debug\regctrl.exe" "bin\" >nul
    echo regctrl.exe copied to bin\
) else (
    echo regctrl.exe not found!
)

if exist "sys\x64\Debug\regfltr.sys" (
    copy "sys\x64\Debug\regfltr.sys" "bin\" >nul
    echo regfltr.sys copied to bin\
) else (
    echo regfltr.sys not found!
)

echo.
echo Files in bin folder:
dir /b "bin\"
echo.

pause