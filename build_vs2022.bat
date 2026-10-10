@echo off
setlocal
cd /d "%~dp0"
rem Set OPMCHORD_VCVARS to your custom installation's vcvarsall.bat if needed.
if defined OPMCHORD_VCVARS goto found
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" set "OPMCHORD_VCVARS=%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat"
if defined OPMCHORD_VCVARS goto found
for %%R in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if exist "%%~R\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvarsall.bat" set "OPMCHORD_VCVARS=%%~R\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvarsall.bat"
  )
)
if defined OPMCHORD_VCVARS goto found
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\SxS\VS7" /v 17.0 2^>nul') do (
  if exist "%%BVC\Auxiliary\Build\vcvarsall.bat" set "OPMCHORD_VCVARS=%%BVC\Auxiliary\Build\vcvarsall.bat"
)
if defined OPMCHORD_VCVARS goto found
echo Visual Studio 2022 C++ tools were not found.
echo Set OPMCHORD_VCVARS to the full path of VC\Auxiliary\Build\vcvarsall.bat.
echo Example: set "OPMCHORD_VCVARS=D:\VS2022\VC\Auxiliary\Build\vcvarsall.bat"
goto failure
:found
if not exist "%OPMCHORD_VCVARS%" goto failure
if not exist bin mkdir bin
if not exist build mkdir build
call "%OPMCHORD_VCVARS%" x64
if errorlevel 1 goto failure
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /MT /DUNICODE /D_UNICODE /Isrc src\main.cpp /Fobuild\main.obj /Febin\OPMChord.exe /link /SUBSYSTEM:WINDOWS winmm.lib user32.lib gdi32.lib comctl32.lib
if errorlevel 1 goto failure
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /MT /Isrc tests\core_tests.cpp /Fobuild\core_tests.obj /Febuild\core_tests.exe
if errorlevel 1 goto failure
build\core_tests.exe
if errorlevel 1 goto failure
call "%OPMCHORD_VCVARS%" x86
if errorlevel 1 goto failure
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /MT /DUNICODE /D_UNICODE /Isrc src\x68_helper.cpp /Fobuild\x68_helper.obj /Febin\OPMChord-X68Bridge32.exe /link /SUBSYSTEM:CONSOLE
if errorlevel 1 goto failure
echo Build complete: bin\OPMChord.exe
pause
exit /b 0
:failure
echo Build failed. See the output above.
pause
exit /b 1
