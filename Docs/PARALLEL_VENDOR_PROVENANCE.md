# Parallel — arbres figés et provenance

Les arbres Parallel ne sont plus des sous-modules. Ils sont figés dans ce dépôt,
à l'état exact qui a été compilé et validé, et plus rien n'est récupéré depuis
`github.com/Themaister`.

Ce fichier ne crée aucune dépendance réseau. Il existe pour qu'on puisse encore
répondre, dans un an, à « sur quelle base est-on parti » et « qu'a-t-on changé ».

## Ce que nous avons modifié

Trois jeux de modifications, 96 lignes au total, déjà présentes dans les arbres
figés. Elles n'ont plus besoin d'être appliquées : elles *sont* le code.

| Emplacement | Delta | Rôle |
| --- | --- | --- |
| `external/parallel-rdp/parallel-rdp/` (4 fichiers) | +80 lignes | `enqueue_command_batch()`, `drain_commands()`, ring de commandes agrandi. Requis par l'adaptateur RDP optimisé. |
| `external/parallel-rdp/Granite/util/bitops.hpp` | +10 lignes | Contournement MSVC 32 bits : `_BitScanReverse64`/`_BitScanForward64` n'existent pas en x86. **Sans lui, Parallel-RDP ne compile pas en Win32.** Inerte en x64. |
| `external/parallel-rsp/rsp_jit.cpp` | +6 lignes | `code_size += 4096;` dans `init_jit_thunks()` et `jit_region()`. GNU Lightning sous-estime la taille d'émission x86 sur MinGW32 et `jit_emit()` ne peut pas redimensionner un tampon fourni : sans cette marge, pointeur de fonction nul et plantage au lancement. |

### Le patch RSP a été supprimé, et il était déjà cassé

`Source/Script/Patches/parallel-rsp-jit-emission-padding.patch` et le script
`apply_parallel_rsp_patch.cmd` qui l'appliquait ont été retirés, ainsi que leur
appel dans les deux scripts de build.

Ils étaient non seulement devenus inutiles — la source figée porte la
modification — mais **déjà en panne avant le gel**. Le patch écrivait la marge
sous la forme d'une constante `JitEmissionPadding` avec une variable
`allocation_size` distincte, alors que la source de travail portait la forme
`code_size += 4096;`. Le `findstr` du script cherchait la constante, ne la
trouvait pas, puis tentait `git apply --check`, qui échouait à son tour parce
que le contexte des hunks avait changé. Le script sortait donc en erreur et
faisait échouer toute reconstruction des plugins Parallel, sur les deux
architectures.

C'est exactement le premier pas de la procédure décrite dans
`Docs/PARALLEL_X64_STATUS.md`, ce qui explique qu'elle n'ait jamais pu être
menée à bien.

## Deux fausses pistes, réglées définitivement

Tant que ces arbres étaient des sous-modules, Git signalait en permanence des
fichiers « modifiés » qui ne l'étaient pas. C'est ce bruit qui avait fait passer
le correctif Granite ci-dessus pour un inconnu à inspecter.

* `external/parallel-rsp/lightning/` (3 fichiers) : différence de contenu
  **nulle**. `core.autocrlf` est actif sur ce dépôt et réécrivait leurs fins de
  ligne. Réglé par `.gitattributes`, qui met ces arbres en `-text` : les octets
  figés sont exactement ceux donnés au compilateur.
* `.../third_party/spirv-cross/reference/opt/shaders-msl/comp/overlapping-bindings…comp` :
  signalé comme supprimé. Son chemin fait **262 caractères** et Git ne pouvait
  pas l'extraire sous Windows. `core.longpaths` est désormais activé sur le
  dépôt. Ce fichier est une référence de test Metal, sans effet sur la build.

## Exclusions volontaires

* `external/parallel-rsp/win32/mman/sys/.vs/mman/v14/.suo` et
  `mman.vcxproj.user` : état local Visual Studio, non figé.
* `external/parallel-rsp/lightning/gnulib` était un gitlink sans entrée
  `.gitmodules` — jamais extrait, absent du disque, non nécessaire à la build.
  Il disparaît avec le passage en arbre figé.
* `external/sdl` reste un sous-module : il vient de `libsdl-org`, pas de
  l'amont Parallel, et n'entre pas dans ce gel.

## Bases amont figées

État des 31 arbres au moment du gel. Les URL sont indiquées pour mémoire ; rien
dans la build ne les consulte.

| Chemin | Commit | Version amont | Origine (pour mémoire) |
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

Les notices d'origine sont conservées dans chaque arbre et doivent le rester.

* `external/parallel-rdp` et `Granite` : licence de type MIT.
* `external/parallel-rsp` : mixte, avec `LICENSE.LESSER` (LGPLv3) et
  `LICENSE.MIT`, parce qu'il embarque **GNU Lightning**
  (`lightning/COPYING` GPLv3, `COPYING.LESSER` LGPLv3).

Toute distribution binaire incluant la RSP reste soumise aux obligations LGPL
de mise à disposition de la source. Le gel ne change pas cette situation ; il
rend simplement la source correspondante disponible au même endroit que le
reste.
