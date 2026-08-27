# Third-party license notices

Project64JFG packages third-party plugins alongside the emulator. This folder
collects the license texts and release notices for the components included in a
binary ZIP.

| Packaged file | Component | License | Text |
| --- | --- | --- | --- |
| `Plugin\GFX\Project64-ParallelRDP.dll` | Parallel-RDP | MIT | [parallel-rdp.LICENSE.txt](parallel-rdp.LICENSE.txt) |
| `Plugin\RSP\Project64-ParallelRSP.dll` | Parallel-RSP | MIT (dual MIT / LGPLv3; MIT selected) | [parallel-rsp.LICENSE.MIT.txt](parallel-rsp.LICENSE.MIT.txt) |
| `Plugin\RSP\Project64-ParallelRSP.dll` (statically linked) | GNU Lightning | LGPLv3 (or GPLv3; LGPLv3 selected) | [parallel-rsp.GNU-Lightning.NOTICE.txt](parallel-rsp.GNU-Lightning.NOTICE.txt), [LGPL-3.0.txt](LGPL-3.0.txt), [GPL-3.0.txt](GPL-3.0.txt) |

## Parallel plugin source and replacement path

The packaged Parallel DLLs are built from this repository, not inherited
prebuilt binaries. Their Project64 adapters are in
`Source/Project64-parallel-rdp` and `Source/Project64-parallel-rsp`; the exact
vendored upstream trees are in `external/parallel-rdp` and
`external/parallel-rsp`. The vendor bases and local changes are recorded in
[../Docs/PARALLEL_VENDOR_PROVENANCE.md](../Docs/PARALLEL_VENDOR_PROVENANCE.md).

For every public binary release, the release notes must name the immutable tag
that contains those sources and the build scripts. A recipient can build a
replacement `Project64-ParallelRSP.dll` or `Project64-ParallelRDP.dll` from that
tag using `Source\Script\build_parallel_win32.cmd` or
`Source\Script\build_parallel_x64.cmd`, then replace the matching DLL under
`Plugin\GFX` or `Plugin\RSP`. See [../Docs/BUILDING.md](../Docs/BUILDING.md).

## GPL-2.0 components

The emulator core (Project64), Project64-Video, Project64-RSP, Project64-Input,
Project64-Audio, and PJ64 NRage plugins are licensed under **GPL-2.0**.

- Their license text is [../license.md](../license.md); the release packager
  copies that file to the root of each ZIP.
- A binary release must identify its immutable tag as the corresponding source.
