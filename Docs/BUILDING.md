Project64JFG - Building from source
===================================

This document describes the Windows build setup for Project64JFG, a
Project64 fork focused on Jet Force Gemini.

## Required software

* [Git](https://git-scm.com/downloads)
* Visual Studio 2022 with the **Desktop development with C++** workload

The source-built Parallel plugin pairs additionally require:

* Python 3.12 or newer. The scripts use the `py` launcher when available and
  also detect the normal per-user Python installation paths.
* [MSYS2](https://www.msys2.org/) installed in `C:\msys64`.
* For `Win32`: `mingw-w64-ucrt-x86_64-cmake`, `mingw-w64-i686-gcc`, and
  `mingw-w64-i686-make`.
* For `x64`: `mingw-w64-ucrt-x86_64-toolchain`,
  `mingw-w64-ucrt-x86_64-cmake`, and `mingw-w64-ucrt-x86_64-ninja`.

Install the MSYS2 packages from the UCRT64 shell. For example, the Win32 build
uses:

```
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-i686-gcc mingw-w64-i686-make
```

## Clone the repository

The Parallel-RDP and Parallel-RSP trees are vendored: they live in this
repository at the state that was built and validated, so a clone carries them
already and nothing is fetched from upstream. Their frozen bases and the
changes we carry are recorded in `Docs/PARALLEL_VENDOR_PROVENANCE.md`.

SDL is still a submodule and is the only one left to initialize.

```
git clone <repository-url>
cd <clone-directory>
git submodule update --init external/sdl
```

Enable long paths before the first checkout. One vendored SPIR-V-Cross test
reference is 262 characters deep, past the classic Windows limit, and Git
silently skips it otherwise.

```
git config --global core.longpaths true
```

## Build in Visual Studio

Open `Project64.sln` and build either `Release|Win32` or `Release|x64`. It
generates the emulator together with the Project64 Audio and Project64 Input
plugins. Parallel-RDP and Parallel-RSP use their own CMake projects because
Parallel-RSP needs a MinGW-compatible JIT toolchain.

Set `Project64` as the startup project if you want F5 to launch the emulator
from Visual Studio. For a Release build, use **Build Solution**: the
`Project64-Parallel` utility project runs the corresponding Parallel script
after the emulator and writes the plugin pair directly next to it. The scripts
remain available for manual Parallel-only rebuilds.

## Parallel plugin build

The repository contains Project64 source adapters that build against the
vendored Parallel-RDP and Parallel-RSP source trees. Build just the graphics/RSP
pair with:

```
Source\Script\build_parallel_win32.cmd
Source\Script\build_parallel_x64.cmd
```

The first invocation compiles Granite, Shaderc, and the generated SPIR-V shader
header, so it takes noticeably longer than later incremental builds. Each
script writes the pair to
`Bin\<platform>\Release\Plugin\GFX\Project64-ParallelRDP.dll` and
`Bin\<platform>\Release\Plugin\RSP\Project64-ParallelRSP.dll`.

The scripts place their intermediate CMake trees in `C:\pj64-build` to avoid
Windows' legacy 260-character path limit in Granite's nested dependencies.
They automatically recreate a CMake tree whose cache belongs to a different
checkout. That directory can also be deleted manually when no Parallel build is
running.

Parallel-RSP no longer needs a patch step: the extra JIT emission space it
requires on Win32 is part of the vendored `external\parallel-rsp\rsp_jit.cpp`.
Without that headroom GNU Lightning can produce a null function pointer and
crash when the game starts. See `Docs\PARALLEL_VENDOR_PROVENANCE.md`.

This integration requires a Vulkan 1.3-capable GPU and driver. There is no
automated release-packaging step; assemble any distributable manually from the
rebuilt executable, plugins, configuration, language, and license files.
For a public binary release, build from an immutable release tag and name that
tag as the corresponding source in the release notes. The source and replacement
path for the statically linked GNU Lightning component are described in
`Licenses\parallel-rsp.GNU-Lightning.NOTICE.txt`.

## Development configuration

`Config\Project64.cfg.development` keeps the shared `Config` and `Lang` data
available to builds under `Bin\Win32\Release` or `Bin\x64\Release`. Plugin,
save, screenshot, and texture directories use paths relative to the executable.

The Project64 project copies this template to `Config\Project64.cfg` and to the
output directory when those files do not already exist. Both generated files are
ignored by Git, so personal emulator settings do not get committed.
