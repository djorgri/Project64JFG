# Parallel x64 - status report

Status recorded on 8 August 2026, from the configuration, the scripts, the
local artefacts and the repository's Git history.

> **Later developments.** This is a dated snapshot, kept for the reasoning it
> records. Since it was written: the x64 and Win32 pairs are both rebuilt by
> the solution build and shipped in every release; `New-ReleasePackages.ps1`
> at the repository root produces the release ZIPs (the "no export or ZIP
> script" statement below no longer holds); 60 fps is the default target;
> and the reproducibility work proposed at the end has been done, see
> `Docs/PARALLEL_VENDOR_PROVENANCE.md`.

## Conclusion

The x64 path is already integrated: the executable, the Parallel-RDP and
Parallel-RSP adapters, the build scripts and the x64 export exist. This is
therefore not a port to start over, but a cycle of rebuilding, validation and
measurement.

The current x64 export must not however be used as a performance reference.
Its Parallel DLLs are older than the recent optimisations made for Win32. The
x64 pair has to be rebuilt from the current sources first, then tested with a
reproducible JFG scene.

## What already works

* Both source adapters are present: `Source/Project64-parallel-rdp` and
  `Source/Project64-parallel-rsp`.
* The Parallel trees are frozen in `external/parallel-rdp` and
  `external/parallel-rsp`, our modifications included.
* `Source/Script/build_parallel_x64.cmd` builds the x64 pair: Parallel-RDP
  with CMake / Visual Studio and Parallel-RSP with MSYS2 UCRT64 / Ninja.
* The `Project64.sln` solution builds the emulator with the Project64 Audio
  and Project64 Input plugins. There was no export or ZIP script at the time;
  a distribution was assembled by hand from the rebuilt binaries.
* Project64's defaults select `GFX/Project64-ParallelRDP.dll` and
  `RSP/Project64-ParallelRSP.dll`.

The Project64 core also has a native x64 recompiler. The emulator and every
loaded plugin must obviously share the same architecture.

## State of the local artefacts

The mismatch noted on 8 August - x64 DLLs from the 4th and 5th, older than
the command optimisations of the 6th - **is resolved**. Both pairs were
rebuilt from the frozen trees, with the SIMD fix below:

| Artefact | Timestamp |
| --- | --- |
| `Bin/Win32/Release/Plugin/{GFX,RSP}` | 8 August, 15:58 |
| `Bin/x64/Release/Plugin/{GFX,RSP}` | 8 August, 18:48 |

The Win32 pair predates the SIMD fix without that being a problem: its flags
were already those, and rebuilding it produces no new object. Both pairs
therefore come from the same source state.

`Export/x64/` was still to be regenerated before any measurement on the
export.

## What the Win32 optimisations bring to x64

Commit `b54a9e7` ("Optimize Win32 Parallel RDP command processing for
improved 60 FPS performance") carries a Win32 name, but most of its changes
are in the shared adapters:

* RDP: batching of small RDP commands, larger ring buffer, asynchronous
  command processing, triple scanout readback, non-blocking presentation and
  a measurement log.
* RSP: instrumentation of the task cost and of the RDP callback.

These sources are shared by the Win32 and x64 scripts. An x64 rebuild
includes them automatically; there is no second port to write for this part.

**Resolved.** The RSP build only enabled `-mssse3 -msse4.1` on a four-byte
pointer, i.e. only for Win32. GCC's x86-64 baseline stopping at SSE2, the x64
build compiled the vector core's fallbacks - notably that of
`rsp_vect_load_and_shuffle_operand`, executed for every operand of every
vector instruction: instead of a register-to-register `pshufb`, a vector
store, sixteen scalar byte accesses and a reload, with the matching
store-forwarding stall. Nineteen other sites select blend and clamp paths on
SSE4.1.

x64 was therefore **slower than Win32**, not the other way round. The guard
now tests `MINGW` alone. Measured on `rsp/vfunctions.cpp`: 0 SSSE3/SSE4.1
instructions before, 63 after, for an object 452 bytes smaller. Win32 is
unchanged - its flags were already those, and recompiling it produces no new
object.

SSE4.1 is the useful ceiling: the vector core has no AVX path, so raising the
CPU floor further would cost compatibility without bringing anything.

Consequence for what follows: any x64 measurement taken before this fix does
not compare architectures, it measures that handicap.

JFG's shadows rely on RDP/CPU synchronisation at `SyncFull`. That barrier
necessarily limits the possible gains; removing it to gain frames per second
would risk reintroducing rendering defects that were already fixed.

## Reproducibility debt - resolved

The Parallel trees are no longer submodules: they are frozen in the
repository, at the compiled and validated state, and nothing is fetched from
upstream any more. Our three sets of modifications are now part of the
source. The details, the frozen upstream bases and the licences are in
`Docs/PARALLEL_VENDOR_PROVENANCE.md`.

Two findings of that clean-up are worth keeping here:

* The versioned RSP patch **was broken and made every Parallel rebuild
  fail**, on both architectures. It wrote the JIT headroom as a
  `JitEmissionPadding` constant, while the source carried
  `code_size += 4096;`: the script's test did not find the constant, then
  `git apply --check` failed in turn on a changed context. That was step 1 of
  both build scripts, which explains why the procedure below could never be
  completed. Patch and script are removed.
* Part of the "noise" that hid these modifications was not noise: three GNU
  Lightning files had no content difference (line endings rewritten by
  `core.autocrlf`), and the spirv-cross file reported as deleted simply has a
  262-character path that Git could not check out on Windows. `.gitattributes`
  and `core.longpaths` settle both.

## Recommended procedure

1. Initialise the only remaining dependency:

   ```bat
   git submodule update --init external/sdl
   ```

2. Check the x64 prerequisites: Visual Studio 2022, Python 3.12+, MSYS2
   UCRT64, `mingw-w64-ucrt-x86_64-toolchain`, CMake and Ninja, plus a Vulkan
   1.3 driver.
3. Open `Project64.sln` in Visual Studio and build `Release|x64`, then run
   `Source\Script\build_parallel_x64.cmd` if the Parallel DLLs have to be
   rebuilt.
4. Test the EXE under `Bin/x64/Release` with the matching Parallel DLLs.
5. Measure the same JFG state, with the same Parallel settings, in quiet and
   busy scenes, at 30 fps and then at 60 fps.
6. Systematically check: cold start, title -> game transition, save-state
   load, emulator shutdown, shadows, text/HUD, absence of audio crackle and
   video cadence.
7. Consult `Logs/Project64-ParallelRDP.log` and
   `Logs/Project64-ParallelRSP.log` to separate a GPU / `SyncFull` stall, a
   presentation cost, an RDP command throughput issue or an expensive RSP
   task.

## Decision criteria

Adopt x64 as the main platform when the rebuilt pair:

* does not regress visually against Win32;
* starts, loads save states and shuts down reliably;
* keeps at least performance equivalent to Win32 in the selected JFG scenes;
* brings a measurable gain, or at least better headroom for future
  optimisations;
* can be rebuilt from a clean clone - achieved since the Parallel trees were
  frozen, provided `core.longpaths` is enabled before the first checkout.

The 60 fps mode has to be judged separately: its current limits mix
Parallel/RSP load, audio synchronisation and the JFG hacks. An x64
improvement alone therefore does not guarantee a stable 60 fps.

## Proposed next step (as of 8 August)

Make the `external/parallel-rdp` modifications reproducible, rebuild the x64
pair from the 6 August sources, then run a strictly identical Win32/x64
benchmark with the Parallel logs enabled. That is the shortest way to know
whether the bottleneck is the host architecture, the RDP, the RSP, the
presentation or the synchronisation JFG needs.
