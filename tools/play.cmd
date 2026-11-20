@echo off
rem Starts FourSwordsRecomp (docs/PLAYTEST.md). The executable is built with
rem MSYS2 MinGW64 and needs its runtime (SDL2.dll, libstdc++) first on PATH;
rem other copies of those DLLs on the PATH (Git's ucrt64, for example) are not
rem compatible. Runs from the repository root so game.toml and saves\ are found.
rem Arguments are passed on: tools\play.cmd --version
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
cd /d "%~dp0.."
if not exist "C:\msys64\mingw64\bin\SDL2.dll" (
    echo MSYS2 MinGW64 with SDL2 was not found in C:\msys64\mingw64\bin.
    pause
    exit /b 1
)
"build\FourSwordsRecomp.exe" %*
if errorlevel 1 (
    echo.
    echo FourSwordsRecomp ended with error %errorlevel%. Details: build\fsr_local_last.log
    pause
)
endlocal
