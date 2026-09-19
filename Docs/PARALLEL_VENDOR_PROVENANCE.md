# Parallel - vendored trees and provenance

The Parallel trees are no longer submodules. They are frozen in this
repository, at the exact state that was compiled and validated, and nothing
is fetched from `github.com/Themaister` any more.

This file creates no network dependency. It exists so that, a year from now,
"which base did we start from" and "what did we change" can still be
answered.

## What we modified

Three sets of changes, 96 lines in total, already present in the frozen
trees. They no longer need applying: they *are* the code.

| Location | Delta | Role |
| --- | --- | --- |
| `external/parallel-rdp/parallel-rdp/` (4 files) | +80 lines | `enqueue_command_batch()`, `drain_commands()`, enlarged command ring. Required by the optimised RDP adapter. |
| `external/parallel-rdp/Granite/util/bitops.hpp` | +10 lines | 32-bit MSVC workaround: `_BitScanReverse64`/`_BitScanForward64` do not exist on x86. **Without it, Parallel-RDP does not compile for Win32.** Inert on x64. |
| `external/parallel-rsp/rsp_jit.cpp` | +6 lines | `code_size += 4096;` in `init_jit_thunks()` and `jit_region()`. GNU Lightning underestimates the x86 emission size on MinGW32 and `jit_emit()` cannot grow a caller-supplied buffer: without this headroom, a null function pointer and a crash at launch. |

### The RSP patch was removed, and it was already broken

`Source/Script/Patches/parallel-rsp-jit-emission-padding.patch` and the
`apply_parallel_rsp_patch.cmd` script that applied it were removed, together
with their call in the two build scripts.

They had not only become useless - the frozen source carries the change - but
were **already broken before the freeze**. The patch wrote the headroom as a
`JitEmissionPadding` constant with a separate `allocation_size` variable,
while the working source carried the form `code_size += 4096;`. The script's
`findstr` looked for the constant, did not find it, then tried
`git apply --check`, which failed in turn because the hunk context had
changed. The script therefore exited with an error and made every Parallel
plugin rebuild fail, on both architectures.

That is exactly the first step of the procedure described in
`Docs/PARALLEL_X64_STATUS.md`, which explains why it could never be completed.

## Two false leads, settled for good

While these trees were submodules, Git constantly reported "modified" files
that were not. That noise is what had made the Granite fix above look like
an unknown to inspect.

* `external/parallel-rsp/lightning/` (3 files): content difference **nil**.
  `core.autocrlf` is active on this repository and rewrote their line
  endings. Settled by `.gitattributes`, which marks these trees `-text`: the
  frozen bytes are exactly those given to the compiler.
* `.../third_party/spirv-cross/reference/opt/shaders-msl/comp/overlapping-bindings...comp`:
  reported as deleted. Its path is **262 characters** long and Git could not
  check it out on Windows. `core.longpaths` is now enabled on the repository.
  This file is a Metal test reference, with no effect on the build.

## Deliberate exclusions

* `external/parallel-rsp/win32/mman/sys/.vs/mman/v14/.suo` and
  `mman.vcxproj.user`: local Visual Studio state, not frozen.
* `external/parallel-rsp/lightning/gnulib` was a gitlink without a
  `.gitmodules` entry - never checked out, absent from disk, not needed by
  the build. It disappears with the move to a frozen tree.
* `external/sdl` remains a submodule: it comes from `libsdl-org`, not from
  the Parallel upstream, and is not part of this freeze.

## Frozen upstream bases

State of the 31 trees at the time of the freeze. The URLs are given for the
record; nothing in the build consults them.

| Path | Commit | Upstream version | Origin (for the record) |
| --- | --- | --- | --- |
| `external/parallel-rdp/Granite/third_party/astc-encoder/Source/GoogleTest` | `e2239ee6043f` | release-1.11.0 | https://github.com/google/googletest.git |
| `external/parallel-rdp/Granite/third_party/astc-encoder` | `77e0ce653414` | 4.2.0-5-g77e0ce6 | https://github.com/ARM-software/astc-encoder |
| `external/parallel-rdp/Granite/third_party/fossilize/cli/SPIRV-Cross` | `5e963d62fa3f` | sdk-1.3.261.0-20-g5e963d62 | https://github.com/KhronosGroup/SPIRV-Cross |
| `external/parallel-rdp/Granite/third_party/fossilize/cli/SPIRV-Headers` | `fc7d24627651` | sdk-1.3.261.0-8-gfc7d246 | https://github.com/KhronosGroup/SPIRV-Headers |
| `external/parallel-rdp/Granite/third_party/fossilize/cli/SPIRV-Tools` | `a996591b1c67` | sdk-1.3.261.0-41-ga996591b | https://github.com/KhronosGroup/SPIRV-Tools |
| `external/parallel-rdp/Granite/third_party/fossilize/cli/dirent` | `c885633e126a` | 1.23-38-gc885633 | https://github.com/tronkko/dirent |
| `external/parallel-rdp/Granite/third_party/fossilize/cli/volk` | `65e811e401bc` | sdk-1.3.261.0-10-g65e811e | https://github.com/zeux/volk |
| `external/parallel-rdp/Granite/third_party/fossilize/rapidjson/thirdparty/gtest` | `ba96d0b1161f` | release-1.8.0-1003-gba96d0b1 | https://github.com/google/googletest.git |
| `external/parallel-rdp/Granite/third_party/fossilize/rapidjson` | `8f4c021fa2f1` | v1.1.0-549-g8f4c021f | https://github.com/miloyip/rapidjson |
| `external/parallel-rdp/Granite/third_party/fossilize` | `692d3958df80` | 692d395 | https://github.com/ValveSoftware/Fossilize |
| `external/parallel-rdp/Granite/third_party/fsr2` | `9319ad08ff74` | v2.0.1a-43-g9319ad0 | https://github.com/Themaister/FidelityFX-FSR2 |
| `external/parallel-rdp/Granite/third_party/glslang` | `7c3c50ea9435` | 14.2.0-9-g7c3c50ea | https://github.com/KhronosGroup/glslang.git |
| `external/parallel-rdp/Granite/third_party/khronos/vulkan-headers` | `31aa7f634b05` | v1.3.278 | https://github.com/KhronosGroup/Vulkan-Headers |
| `external/parallel-rdp/Granite/third_party/meshoptimizer` | `eb385d6987d1` | v0.19-42-geb385d69 | https://github.com/zeux/meshoptimizer |
| `external/parallel-rdp/Granite/third_party/muFFT` | `47bb08652eab` | 47bb086 | https://github.com/Themaister/muFFT |
| `external/parallel-rdp/Granite/third_party/oboe` | `11ea86592ef8` | 1.6.1-122-g11ea8659 | https://github.com/google/oboe |
| `external/parallel-rdp/Granite/third_party/pyroenc/vulkan-header` | `1e7b8a6d03d3` | v1.3.282 | https://github.com/KhronosGroup/Vulkan-Headers |
| `external/parallel-rdp/Granite/third_party/pyroenc` | `ab4169c3714d` | ab4169c | https://github.com/HansKristian-Work/pyroenc |
| `external/parallel-rdp/Granite/third_party/rapidjson/thirdparty/gtest` | `ba96d0b1161f` | release-1.8.0-1003-gba96d0b1 | https://github.com/google/googletest.git |
| `external/parallel-rdp/Granite/third_party/rapidjson` | `06d58b9e848c` | v1.1.0-709-g06d58b9e | https://github.com/miloyip/rapidjson |
| `external/parallel-rdp/Granite/third_party/sdl3` | `8c25129458a7` | release-2.26.0-5338-g8c2512945 | https://github.com/libsdl-org/SDL |
| `external/parallel-rdp/Granite/third_party/shaderc` | `f59f0d11b80f` | v2024.1-1-gf59f0d1 | https://github.com/google/shaderc.git |
| `external/parallel-rdp/Granite/third_party/spirv-cross` | `476f384eb7d9` | vulkan-sdk-1.3.283.0-2-g476f384e | https://github.com/KhronosGroup/SPIRV-Cross |
| `external/parallel-rdp/Granite/third_party/spirv-headers` | `49a1fceb9b1d` | vulkan-sdk-1.3.283.0-3-g49a1fce | https://github.com/KhronosGroup/SPIRV-Headers |
| `external/parallel-rdp/Granite/third_party/spirv-tools` | `199038f10cbe` | v2024.2-4-g199038f1 | https://github.com/KhronosGroup/SPIRV-Tools |
| `external/parallel-rdp/Granite/third_party/stb/stb` | `c9064e317699` | c9064e3 | https://github.com/nothings/stb |
| `external/parallel-rdp/Granite/third_party/volk` | `41c8f70e8051` | vulkan-sdk-1.3.268.0-2-g41c8f70 | https://github.com/zeux/volk |
| `external/parallel-rdp/Granite` | `cf71dee71fb0` | cf71dee7 | https://github.com/Themaister/Granite |
| `external/parallel-rdp/angrylion-rdp-plus` | `31bdb1f0a79d` | r8-26-g31bdb1f | https://github.com/Themaister/angrylion-rdp-plus |
| `external/parallel-rdp` | `1cecd042b261` | 1cecd042 | https://github.com/Themaister/parallel-rdp.git |
| `external/parallel-rsp` | `9ab776a2ce28` | 9ab776a | https://github.com/Themaister/parallel-rsp.git |

## Licences

The original notices are kept in each tree and must stay there.

* `external/parallel-rdp` and `Granite`: MIT-style licence.
* `external/parallel-rsp`: mixed, with `LICENSE.LESSER` (LGPLv3) and
  `LICENSE.MIT`, because it embeds **GNU Lightning** (`lightning/COPYING`
  GPLv3, `COPYING.LESSER` LGPLv3).

Any binary distribution that includes the RSP remains subject to the LGPL
obligations to make the source available. The freeze does not change that
situation; it simply makes the corresponding source available in the same
place as the rest.
