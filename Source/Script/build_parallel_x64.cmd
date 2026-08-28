@ECHO OFF
SETLOCAL EnableExtensions

set "current_dir=%cd%"
cd /d "%~dp0..\.."
set "base_dir=%cd%"
cd /d "%current_dir%"

set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "cmake="
if exist "%vswhere%" (
	for /f "usebackq delims=" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "cmake=%%i"
)
if not defined cmake (
	echo Cannot find Visual Studio CMake. Install Visual Studio 2022 with the Desktop development with C++ workload.
	goto :EndErr
)

set "python="
for /f "usebackq delims=" %%i in (`py -3 -c "import sys; print(sys.executable)" 2^>nul`) do set "python=%%i"
for %%v in (313 312 311 310) do if not defined python if exist "%LocalAppData%\Programs\Python\Python%%v\python.exe" set "python=%LocalAppData%\Programs\Python\Python%%v\python.exe"
if not defined python (
	echo Cannot find Python 3. Install Python 3.12 or later and ensure the py launcher is available.
	goto :EndErr
)

set "msys_bash=C:\msys64\usr\bin\bash.exe"
set "msys_ucrt_bin=C:\msys64\ucrt64\bin"
if not exist "%msys_bash%" (
	echo Cannot find MSYS2 UCRT64. Install MSYS2 and its mingw-w64-ucrt-x86_64-toolchain, cmake and ninja packages.
	goto :EndErr
)
if not exist "%msys_ucrt_bin%\gcc.exe" (
	echo Cannot find the UCRT64 C compiler. Install the mingw-w64-ucrt-x86_64-toolchain MSYS2 package.
	goto :EndErr
)
if not exist "%msys_ucrt_bin%\g++.exe" (
	echo Cannot find the UCRT64 C++ compiler. Install the mingw-w64-ucrt-x86_64-toolchain MSYS2 package.
	goto :EndErr
)
if not exist "%msys_ucrt_bin%\cmake.exe" (
	echo Cannot find UCRT64 CMake. Install the mingw-w64-ucrt-x86_64-cmake MSYS2 package.
	goto :EndErr
)
if not exist "%msys_ucrt_bin%\ninja.exe" (
	echo Cannot find UCRT64 Ninja. Install the mingw-w64-ucrt-x86_64-ninja MSYS2 package.
	goto :EndErr
)

rem Granite's nested CMake projects exceed MAX_PATH under a normal checkout.
set "parallel_build_root=%SystemDrive%\pj64-build"
if not exist "%parallel_build_root%" mkdir "%parallel_build_root%"
if errorlevel 1 goto :EndErr
set "parallel_rdp_build_dir=%parallel_build_root%\parallel-rdp-x64"
set "parallel_rsp_build_dir=%parallel_build_root%\parallel-rsp-x64"
set "parallel_rsp_build_dir_msys=/%SystemDrive:~0,1%/pj64-build/parallel-rsp-x64"

for %%i in ("%base_dir%") do set "base_dir_short=%%~si"

call :EnsureMatchingCmakeCache "%parallel_rdp_build_dir%" "%base_dir%\Source\Project64-parallel-rdp"
if errorlevel 1 goto :EndErr

echo Building Project64 Parallel RDP x64
"%cmake%" -S "%base_dir%\Source\Project64-parallel-rdp" -B "%parallel_rdp_build_dir%" -G "Visual Studio 17 2022" -A x64 -DPython_EXECUTABLE="%python%" -DPython3_EXECUTABLE="%python%"
if errorlevel 1 goto :EndErr
"%cmake%" --build "%parallel_rdp_build_dir%" --config Release --target Project64-ParallelRDP --parallel 1
if errorlevel 1 goto :EndErr

set "msys_base=%base_dir_short:\=/%"
set "msys_base=/%msys_base:~0,1%%msys_base:~2%"

call :EnsureMatchingCmakeCache "%parallel_rsp_build_dir%" "%base_dir_short%\Source\Project64-parallel-rsp"
if errorlevel 1 goto :EndErr

echo Building Project64 Parallel RSP x64
"%msys_bash%" -lc "export PATH=/ucrt64/bin:$PATH; cmake -S '%msys_base%/Source/Project64-parallel-rsp' -B '%parallel_rsp_build_dir_msys%' -G Ninja -DCMAKE_BUILD_TYPE=Release -DPARALLEL_RSP_TESTS=OFF -DPARALLEL_RSP_DEBUG_JIT=OFF"
if errorlevel 1 goto :EndErr
"%msys_bash%" -lc "export PATH=/ucrt64/bin:$PATH; cmake --build '%parallel_rsp_build_dir_msys%' --target Project64-ParallelRSP --parallel 1"
if errorlevel 1 goto :EndErr

echo Parallel x64 plugins built successfully
goto :End

:EndErr
if defined current_dir cd /d "%current_dir%"
ENDLOCAL
echo Parallel x64 build failed
exit /B 1

:End
if defined current_dir cd /d "%current_dir%"
ENDLOCAL
exit /B 0

:EnsureMatchingCmakeCache
set "cache_dir=%~1"
set "source_dir=%~2"
if not exist "%cache_dir%\CMakeCache.txt" exit /B 0
set "source_dir_cmake=%source_dir:\=/%"
findstr /I /X /C:"CMAKE_HOME_DIRECTORY:INTERNAL=%source_dir_cmake%" "%cache_dir%\CMakeCache.txt" >nul
if not errorlevel 1 exit /B 0

echo Removing stale CMake cache: %cache_dir%
rmdir /S /Q "%cache_dir%"
if exist "%cache_dir%" exit /B 1
exit /B 0
