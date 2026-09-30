@echo off
rem ============================================================================
rem  Shared MSVC environment setup for build.bat and tests\run_*.bat.
rem  Loads vcvars64.bat into the CALLER's environment, so there is deliberately
rem  no setlocal here. Works with any VS 2022 edition (the GitHub windows-2022
rem  runner ships Enterprise, local machines usually have Community).
rem ============================================================================

rem Already inside a developer environment: nothing to do.
if defined VCToolsInstallDir exit /b 0

set "FT_VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined FT_VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "FT_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined FT_VCVARS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "FT_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not defined FT_VCVARS (
    echo HATA: Visual Studio C++ araclari bulunamadi!
    exit /b 1
)

echo Visual Studio ortami: %FT_VCVARS%
call "%FT_VCVARS%" >nul
set "FT_VCVARS="
if not defined VCToolsInstallDir (
    echo HATA: vcvars64.bat ortami kuramadi!
    exit /b 1
)
exit /b 0
