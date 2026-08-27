@ECHO OFF
SETLOCAL EnableExtensions

set "current_dir=%cd%"
cd /d "%~dp0\..\.."
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

set "msys_cmake=C:\msys64\ucrt64\bin\cmake.exe"
set "mingw32_bin=C:\msys64\mingw32\bin"
if not exist "%msys_cmake%" (
	echo Cannot find MSYS2 CMake. Install MSYS2 with the mingw-w64-ucrt-x86_64-cmake package.
	goto :EndErr
)
if not exist "%mingw32_bin%\gcc.exe" (
	echo Cannot find the MinGW 32-bit compiler. Install mingw-w64-i686-gcc and mingw-w64-i686-make in MSYS2.
	goto :EndErr
)
if not exist "%mingw32_bin%\mingw32-make.exe" (
	echo Cannot find MinGW 32-bit Make. Install mingw-w64-i686-make in MSYS2.
	goto :EndErr
)

rem Granite's nested CMake projects exceed MAX_PATH under a normal checkout.
set "parallel_build_root=%SystemDrive%\pj64-build"
if not exist "%parallel_build_root%" mkdir "%parallel_build_root%"
if errorlevel 1 goto :EndErr
set "parallel_rdp_build_dir=%parallel_build_root%\parallel-rdp-win32"
set "parallel_rsp_build_dir=%parallel_build_root%\parallel-rsp-win32-mingw"

echo Building Project64 Parallel RDP Win32
"%cmake%" -S "%base_dir%\Source\Project64-parallel-rdp" -B "%parallel_rdp_build_dir%" -G "Visual Studio 17 2022" -A Win32 -DPython_EXECUTABLE="%python%" -DPython3_EXECUTABLE="%python%"
if errorlevel 1 goto :EndErr
"%cmake%" --build "%parallel_rdp_build_dir%" --config Release --target Project64-ParallelRDP --parallel 1
if errorlevel 1 goto :EndErr

echo Building Project64 Parallel RSP Win32
set "PATH=%mingw32_bin%;C:\msys64\ucrt64\bin;%PATH%"
"%msys_cmake%" -S "%base_dir%\Source\Project64-parallel-rsp" -B "%parallel_rsp_build_dir%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=%mingw32_bin%\gcc.exe -DCMAKE_CXX_COMPILER=%mingw32_bin%\g++.exe -DCMAKE_MAKE_PROGRAM=%mingw32_bin%\mingw32-make.exe
if errorlevel 1 goto :EndErr
"%msys_cmake%" --build "%parallel_rsp_build_dir%" --target Project64-ParallelRSP --parallel 1
if errorlevel 1 goto :EndErr

echo Parallel Win32 plugins built successfully
goto :End

:EndErr
if defined current_dir cd /d "%current_dir%"
ENDLOCAL
echo Parallel Win32 build failed
exit /B 1

:End
if defined current_dir cd /d "%current_dir%"
ENDLOCAL
exit /B 0
