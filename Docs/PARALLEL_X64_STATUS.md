# Parallel x64 — état des lieux

État relevé le 8 août 2026, à partir de la configuration, des scripts, des
artefacts locaux et de l'historique Git du dépôt.

## Conclusion

Le chemin x64 est déjà intégré : l'exécutable, les adaptateurs Parallel-RDP et
Parallel-RSP, les scripts de build et l'export x64 existent. Il ne s'agit donc
pas d'un port à recommencer, mais d'un cycle de reconstruction, de validation
et de mesure.

L'export x64 actuel ne doit cependant pas être utilisé comme référence de
performance. Ses DLL Parallel sont plus anciennes que les optimisations
récentes réalisées pour Win32. Il faut d'abord reconstruire la paire x64 depuis
les sources actuelles, puis la tester avec une scène JFG reproductible.

## Ce qui fonctionne déjà

* Les deux adaptateurs source sont présents :
  `Source/Project64-parallel-rdp` et `Source/Project64-parallel-rsp`.
* Les arbres Parallel sont figés dans `external/parallel-rdp` et
  `external/parallel-rsp`, nos modifications comprises.
* `Source/Script/build_parallel_x64.cmd` construit la paire x64 :
  Parallel-RDP avec CMake / Visual Studio et Parallel-RSP avec MSYS2 UCRT64 /
  Ninja.
* La solution `Project64.sln` construit l'émulateur avec les plugins Project64
  Audio et Project64 Input. Il n'y a pas de script d'export ou de ZIP ; une
  distribution est assemblée manuellement à partir des binaires reconstruits.
* Les valeurs par défaut de Project64 sélectionnent
  `GFX/Project64-ParallelRDP.dll` et `RSP/Project64-ParallelRSP.dll`.

Le cœur Project64 possède également un recompiler x64 natif. L'émulateur et
tous les plugins chargés doivent évidemment avoir la même architecture.

## État des artefacts locaux

Le décalage relevé le 8 août — DLL x64 du 4 et du 5, antérieures aux
optimisations de commande du 6 — **est résorbé**. Les deux paires ont été
reconstruites depuis les arbres figés, avec le correctif SIMD ci-dessous :

| Artefact | Horodatage |
| --- | --- |
| `Bin/Win32/Release/Plugin/{GFX,RSP}` | 8 août, 15h58 |
| `Bin/x64/Release/Plugin/{GFX,RSP}` | 8 août, 18h48 |

Le Win32 date d'avant le correctif SIMD sans que ce soit un problème : ses
drapeaux étaient déjà ceux-là, et sa reconstruction ne produit aucun objet
nouveau. Les deux paires proviennent donc du même état de source.

`Export/x64/` reste à régénérer avant toute mesure sur l'export.

## Ce que les optimisations Win32 apportent à x64

Le commit `b54a9e7` (« Optimize Win32 Parallel RDP command processing for
improved 60 FPS performance ») porte un nom Win32, mais l'essentiel de ses
changements est dans les adaptateurs communs :

* RDP : regroupement des petites commandes RDP, ring buffer plus grand,
  traitement asynchrone des commandes, triple readback de scanout, présentation
  non bloquante et journal de mesures.
* RSP : instrumentation du coût des tâches et du callback RDP.

Ces sources sont partagées par les scripts Win32 et x64. Une reconstruction x64
les inclura automatiquement ; il n'y a pas de second port à écrire pour cette
partie.

**Résolu.** Le build RSP n'activait `-mssse3 -msse4.1` que sur un pointeur de
quatre octets, donc uniquement en Win32. La base x86-64 de GCC s'arrêtant à
SSE2, le x64 compilait les replis du cœur vectoriel — et notamment celui de
`rsp_vect_load_and_shuffle_operand`, exécuté pour chaque opérande de chaque
instruction vectorielle : au lieu d'un `pshufb` registre à registre, un store
vectoriel, seize accès scalaires octet par octet et un rechargement, avec le
blocage de store-forwarding correspondant. Dix-neuf autres sites sélectionnent
des chemins de blend et de clamp sur SSE4.1.

Le x64 était donc **plus lent que le Win32**, et non l'inverse. La garde porte
désormais sur `MINGW` seul. Mesuré sur `rsp/vfunctions.cpp` : 0 instruction
SSSE3/SSE4.1 avant, 63 après, pour un objet plus petit de 452 octets. Le Win32
est inchangé — ses drapeaux étaient déjà ceux-là, et sa recompilation ne produit
aucun objet nouveau.

SSE4.1 est le plafond utile : le cœur vectoriel n'a aucun chemin AVX, donc
relever davantage le plancher CPU coûterait de la compatibilité sans rien
rapporter.

Conséquence pour la suite : toute mesure x64 antérieure à ce correctif ne
compare pas des architectures, elle mesure ce handicap.

Les ombres de JFG reposent sur la synchronisation RDP/CPU à `SyncFull`. Cette
barrière limite nécessairement les gains possibles ; la supprimer pour gagner
des images/seconde risquerait de réintroduire les défauts de rendu déjà corrigés.

## Dette de reproductibilité — résolue

Les arbres Parallel ne sont plus des sous-modules : ils sont figés dans le
dépôt, à l'état compilé et validé, et plus rien n'est récupéré depuis l'amont.
Nos trois jeux de modifications font désormais partie de la source. Le détail,
les bases amont figées et les licences sont dans
`Docs/PARALLEL_VENDOR_PROVENANCE.md`.

Deux constats de ce nettoyage méritent d'être retenus ici :

* Le patch RSP versionné **était cassé et faisait échouer toute reconstruction
  Parallel**, sur les deux architectures. Il écrivait la marge JIT sous forme
  d'une constante `JitEmissionPadding`, alors que la source portait
  `code_size += 4096;` : le test du script ne trouvait pas la constante, puis
  `git apply --check` échouait à son tour sur un contexte modifié. C'était
  l'étape 1 des deux scripts de build, ce qui explique que la procédure
  ci-dessous n'ait jamais pu aboutir. Patch et script sont supprimés.
* Une partie du « bruit » qui masquait ces modifications n'en était pas :
  trois fichiers GNU Lightning n'avaient aucune différence de contenu (fins de
  ligne réécrites par `core.autocrlf`), et le fichier spirv-cross signalé
  supprimé a simplement un chemin de 262 caractères que Git ne pouvait pas
  extraire sous Windows. `.gitattributes` et `core.longpaths` règlent les deux.

## Procédure recommandée

1. Initialiser la seule dépendance restante :

   ```bat
   git submodule update --init external/sdl
   ```

2. Vérifier les prérequis x64 : Visual Studio 2022, Python 3.12+, MSYS2 UCRT64,
   `mingw-w64-ucrt-x86_64-toolchain`, CMake et Ninja, plus un pilote Vulkan 1.3.
3. Ouvrir `Project64.sln` dans Visual Studio et construire `Release|x64`, puis
   lancer `Source\Script\build_parallel_x64.cmd` si les DLL Parallel doivent
   être reconstruites.
4. Tester l'EXE sous `Bin/x64/Release` avec les DLL Parallel correspondantes.
5. Mesurer une même sauvegarde JFG, avec les mêmes réglages Parallel, dans des   scènes calmes et chargées, en 30 fps puis en 60 fps si ce dernier mode est
   réactivé.
6. Vérifier systématiquement : démarrage à froid, passage titre → jeu,
   chargement de save state, fermeture de l'émulateur, ombres, texte/HUD,
   absence de grésillement audio et cadence vidéo.
7. Consulter `Logs/Project64-ParallelRDP.log` et
   `Logs/Project64-ParallelRSP.log` pour séparer un blocage GPU / `SyncFull`,
   un coût de présentation, un débit de commandes RDP ou une tâche RSP coûteuse.

## Critères de décision

Adopter x64 comme plateforme principale lorsque la paire reconstruite :

* ne régresse pas visuellement face à Win32 ;
* démarre, charge les save states et se ferme de façon stable ;
* maintient au minimum des performances équivalentes à Win32 dans les scènes
  JFG retenues ;
* apporte un gain mesurable, ou au moins une meilleure marge pour les futures
  optimisations ;
* est reconstruisible depuis un clone propre — acquis depuis le gel des arbres
  Parallel, sous réserve d'activer `core.longpaths` avant le premier checkout.

Le mode 60 fps doit être jugé séparément : ses limites actuelles mêlent charge
Parallel/RSP, synchronisation audio et hacks JFG. Une amélioration x64 ne
garantit donc pas à elle seule un 60 fps stable.

## Prochaine étape proposée

Rendre les modifications de `external/parallel-rdp` reproductibles, reconstruire
la paire x64 depuis les sources du 6 août, puis faire un benchmark Win32/x64
strictement identique avec les journaux Parallel activés. C'est la manière la
plus courte de savoir si le goulot est l'architecture hôte, le RDP, le RSP, la
présentation ou la synchronisation nécessaire à JFG.
