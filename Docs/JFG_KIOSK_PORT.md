# Portage des hacks JFG vers la ROM Kiosk — état des lieux

ROM cible : `Jet Force Gemini (USA) (Demo) (Kiosk)`, nom interne « J F G DISPLAY ».

| | Identifiant Project64 |
| --- | --- |
| US (supportée) | `8A6009B6-94ACE150-C:45` |
| Kiosk | `DFD8AB47-3CDBEB89-C:45` |

Le mapping ROM du segment de base est **le même que pour l'US** :
`offset ROM = adresse RAM - 0x80000450 + 0x1050`. Vérifié sur les prologues de
`amSetMuteMode`, `wakeUpdateRipple`, `controlGetManualAim` et `controlPlayer`.

## Le résultat qui décide de tout : les structures sont identiques

Les offsets de champ utilisés par le hack apparaissent aux mêmes fréquences dans
les deux images :

| Offset | Champ | Occurrences US | Kiosk |
| --- | --- | --- | --- |
| `0x11C` | yaw de déplacement joueur | 58 | 55 |
| `0x1E2` | pitch de visée | 13 | 15 |
| `0x1E4` / `0x1E8` | vitesses de rotation caméra | 11 / 9 | 11 / 9 |
| `0x568` | mode caméra | 39 | 37 |
| `0x68` | objet → PlayerData | 633 | 590 |
| `0x58` | objet → ripple | 403 | 420 |

Et la correspondance de code confirme au mot près : le `swc1 $f4, 0x1E4($s0)` de
`controlGetManualAim` se retrouve à l'identique dans la Kiosk.

**Conséquence : aucun accès `PlayerData + offset` n'est à retoucher.** Seules les
adresses de code et de globales changent. C'est ce qui rend le portage réaliste.

## Méthode

Le décomp fournit deux tables de symboles, `jfg_us_syms_full.txt` et
`jfg_kiosk_syms_full.txt`. Trois techniques, par ordre de fiabilité :

1. **Symbole + offset** — exact pour les globales, qui tombent presque toutes
   pile sur un symbole nommé (`currentScreen+0`, `playerlist+0`, `disablejoy+0`,
   `controlcam+0`, `ObjList+0`, `animcamera+0`).
2. **Signature d'instructions** — indispensable *dans* les fonctions : la Kiosk
   est une compilation distincte, les corps diffèrent, l'offset ne se conserve
   pas. On compare une fenêtre d'instructions en masquant ce qui doit bouger
   (cibles `jal`/`j`, moitiés hautes de `lui`, déplacements mémoire). Le reste —
   opcodes, registres, immédiats ALU, déplacements de branchement — est comparé
   exactement, donc une correspondance est structurelle et non devinée.
3. **Référence depuis le code** — la plus sûre pour une globale : on relit la
   paire `lui`/`addiu` à l'emplacement Kiosk retrouvé. C'est ainsi qu'on obtient
   `WaterWakeGlobalFade` : US `0x800A6950` → Kiosk `0x800A7310`.

Chaque patch de code porte son mot d'origine attendu, ce qui donne une
vérification indépendante : 18 des 27 sites vérifiables retrouvent leur mot
exact à l'adresse Kiosk calculée.

## Couverture

**87 des 118 constantes d'adresse sont résolues.** Sur les 31 restantes :

* **7 ne sont pas des adresses** — `0x8FBF003C`, `0x8FAD00E8`, `0x86020090`…
  sont des mots d'instruction attendus, capturés à tort par l'extraction. Ils
  devront être relus dans la Kiosk, comme les cibles de `jal` relocalisées.
* **4 sont en RAM d'extension** (`0x8034xxxx`, `0x8035xxxx`) : ce sont des
  adresses résidentes d'overlay, une classe de problème distincte.
* **9 sont des bases de stub** (`0x80066C00`…`0x80067000`).
* **11 sont de vrais points d'entrée** dont 4 ont été résolus en second passage.

### Résolus en second passage, avec marge nette

| Constante | US | Kiosk | Score | Marge sur le 2ᵉ candidat |
| --- | --- | --- | --- | --- |
| `CameraTopDownEntry` | `8002EB6C` | `8002EB78` | 19/21 | +15 |
| `PlayerVelocityEntry` | `800341A4` | `800341B8` | 27/29 | +16 |
| `SidekickLateralMoveEntry` | `80031088` | `80031094` | 26/29 | +18 |
| `SidekickLateralMoveOldTailEntry` | `80031080` | `8003108C` | 25/29 | +16 |

### `SchedulerFrameGateAdd` — résolu à la main

C'est le verrou du 60 fps, et la recherche automatique échouait (6/13) pour une
raison instructive : la Kiosk alloue d'autres registres. La signature comparait
les champs de registre à l'identique, ce qui ne peut pas marcher entre deux
compilations distinctes.

En cherchant la **forme** plutôt que les octets — un `lw` en `0x300`, un `addiu`
de 1 sur ce registre, sa réécriture en `0x300`, et le `sltiu … , 2` qui commande
la libération — le motif n'apparaît **qu'une seule fois dans chaque image**.
Contrôle croisé : sur l'US, la recherche retombe exactement sur `0x800506D8`,
l'adresse connue. Aucune ambiguïté.

Le contexte Kiosk est identique instruction pour instruction, mêmes offsets de
champ et mêmes déplacements de branchement (0x19, 0x16, 0x10, 0x05, 0x06) ;
seuls les registres changent, `t0`/`t1` devenant `t2`/`t3`.

| | US | Kiosk |
| --- | --- | --- |
| `SchedulerSignatureBase` | `800506D0` | **`8005124C`** |
| `SchedulerFrameGateAdd` | `800506D8` | **`80051254`** |
| Mot d'origine | `25090001` | **`254B0001`** |
| Remplacement | `25090002` | **`254B0002`** |
| `Signature[0]` (+0x00) | `8E480300` | `8E4A0300` |
| `Signature[1]` (+0x04) | `8E4302FC` | `8E4302FC` (identique) |
| `Signature[2]` (+0x14) | `2D210002` | `2D610002` |

**Leçon de méthode** : pour les correspondances *dans* une fonction, il faut
que les registres cessent de compter. Les effacer perdrait trop ; on renomme
donc les registres par ordre d'apparition, ce qui conserve les *relations* — la
valeur écrite ici est celle relue deux instructions plus loin — tout en rendant
l'allocation indifférente. `zero`, `sp`, `ra` et `gp` sont laissés tels quels,
n'étant pas allouables. Voir `jfgsig2.py`.

Validation avant usage : sur six adresses dont la correspondance était déjà
établie, la méthode retrouve **6 fois sur 6** la bonne réponse, et
`SchedulerFrameGateAdd` passe de 6/13 à 21/21 avec une marge de +17.

### Résolus par canonisation des registres

Tous confirmés par désassemblage des deux côtés.

| Constante | US | Kiosk | Mot US | Mot Kiosk | Score |
| --- | --- | --- | --- | --- | --- |
| `ManualAimCursorXStore` | `8003B014` | **`8003ADDC`** | `A7190000` | `A5F80000` | 21/21, +10 |
| `ManualAimCursorYStore` | `8003B058` | **`8003AE20`** | `A58D0000` | `A56C0000` | 21/21, +10 |
| `WaterWakeStockDrawEntry` | `80014C44` | **`80014908`** | `0C01ADA7` | `0C01AE78` | 21/21, +12 |
| `WaterWakeDrawFallbackEntry` | `80014CA0` | **`80014964`** | `0C01A2D5` | `0C01A39D` | 26/29, +17 |

Deux recoupements indépendants renforcent ces quatre résultats :

* les deux sites water-wake sont distants de `0x5C` en US **et** en Kiosk ;
* leurs `jal` visent, table de symboles à l'appui, la même fonction nommée des
  deux côtés — `wakeDrawRipple` pour le premier, `fxDrawLevelEffects` pour le
  second. C'est une confirmation sémantique, pas seulement structurelle.

### `CameraHelperBase` — mauvaise classification de ma part

Ce n'est pas un site de code mais un **bourrage de 72 octets** (`800968C8`),
dans lequel le hack installe ses stubs de caméra. Chercher une signature n'y a
aucun sens. Elle rejoint les zones de stub ci-dessous — et la Kiosk n'a rien de
nul à cette adresse, donc il lui faudra un emplacement propre.

### Saut de cinématique d'atterrissage — absent de la Kiosk

`LandingCinematicSkipEntry` et sa variante *legacy* visent une boucle de
`mainGameWindowSize` testant un drapeau `0x1000`. La fonction existe bien en
Kiosk, mais ce code n'y est pas : une recherche sur **tout le segment de base**
plafonne à 6/21 et 5/21, avec une marge nulle — donc du bruit.

La démo Kiosk n'a pas cette séquence. Ce n'est pas un échec de cartographie :
la fonctionnalité est **sans objet** sur cette ROM et n'a pas à être portée.

## Zones de stub — attribuées

### Pourquoi ces adresses sont sûres

Les bases `0x80066C00`…`0x80066F00` ne sont pas du vide : elles tombent dans une
suite de **fonctions statiques de trace CPU**, juste après `diCpuTraceInit`. Le
hack US préserve `diCpuTraceInit` elle-même — la seule appelée, 0x54 octets — et
écrase ses auxiliaires.

Ce critère se vérifie, et il tient à l'identique sur la Kiosk : les cinq
auxiliaires ont **zéro appelant en dehors du bloc**, dans les deux images. Ils ne
sont atteignables que depuis `diCpuTraceInit`.

La suite de fonctions se correspond presque exactement :

| # | US | taille | Kiosk | taille |
| --- | --- | --- | --- | --- |
| 1 `diCpuTraceInit` | `80066B80` | 0x54 | `80067550` | 0x54 |
| 2 | `80066BD4` | 0xE8 | `800675A4` | 0xE8 |
| 3 | `80066CBC` | 0x6C | `8006768C` | 0x6C |
| 4 | `80066D28` | 0xEC | `800676F8` | 0xEC |
| 5 | `80066E14` | 0x9C | `800677E4` | 0xA4 |

Zone exploitable en Kiosk : `800675A4`..`80067920`, soit **0x37C octets** — moins
que les 0xA90 de l'US, d'où la nécessité de compacter plutôt que de conserver les
offsets d'origine.

### Attribution

Taille retenue par lot = la plus grande des fonctionnalités qui s'y partagent la
base, celles-ci étant mutuellement exclusives.

| Lot | US | **Kiosk** | Taille | Fin |
| --- | --- | --- | --- | --- |
| `PlayerVelocity` / `FloydMoveHook` / `SidekickStrafe` | `80066C00` | **`800675B0`** | 0xC0 | `80067670` |
| `SidekickPadProbe` | `80066D80` | **`80067680`** | 0x34 | `800676B4` |
| `ObjectMove` | `80066E00` | **`800676C0`** | 0xD0 | `80067790` |
| `FloydCameraLateral` / `SidekickLateralMove` / `SidekickVelocityLateral` | `80066F00` | **`800677A0`** | 0xC4 | `80067864` |

Sans chevauchement, entièrement dans la zone sûre, **0xBC octets de marge**.

`LandingCinematicSkipStub` (`0x80067000`) disparaît : la fonctionnalité n'existe
pas sur cette ROM, ce qui libère précisément la place qui manquait.

### Bases caméra — bourrages de taille identique

| Base | US | **Kiosk** | Besoin | Bourrage |
| --- | --- | --- | --- | --- |
| `CameraHelperBase` | `800968CC` | **`8009350C`** | 0x40 | 0x48, après `osGetCount` |
| `CameraTopDownHelperBase` | `80098D08` | **`80098CC8`** | 0x18 | 0x18, après `osCreateViManager` |

Vérifié nul sur toute la plage requise dans les deux cas. `CameraHelperBase`
avait par ailleurs été trouvé à la même adresse par la recherche par signature —
deux routes indépendantes qui concordent.

### Stubs Squaddie

Dans la cave de 308 octets, donc directs : `SquaddieXStub` `8009FD60` →
**`800A05F0`**, `SquaddieZStub` `8009FD80` → **`800A0610`**. Plages vérifiées
libres.

## Note historique : le point dur, tel qu'il se présentait

Les bases `0x80066C00`, `0x80066D80`, `0x80066E00`, `0x80066F00` et
`0x80067000` **ne sont pas du vide**, ni en US ni en Kiosk. Le hack y installe
ses stubs parce qu'un jugement a été porté sur le fait que ce code est
inatteignable dans le contexte visé. Ce jugement ne se transpose pas
mécaniquement : il faudra le refaire sur la Kiosk avant d'écrire quoi que ce
soit à ces adresses.

En revanche, le scratch du hack est simple : les variables de travail vivent
dans un bourrage de 308 octets à `0x8009FCAC` en US, et la Kiosk possède un
bourrage **de taille identique** à `0x800A053C`. La correspondance est directe,
offset pour offset.

## Ce qu'il reste à faire

**La cartographie est terminée, la table C++ est écrite, et le code est migré.**

Les 118 champs de `JFG_ADDRESSES` — 110 adresses et 8 mots d'instruction —
alimentent désormais l'ensemble du hack. `ApplyAddressTable()` place les
adresses, recalcule les valeurs dérivées et reconstruit les 241 entrées des
tables de patch ; `SelectAddressTable()` la déclenche au changement de ROM.

Les charges utiles des stubs qui portaient une adresse ne sont plus des
littéraux : elles sont **calculées** depuis la table via `JumpTo`, `CallTo`,
`WithHi` et `WithLo`. Les quinze concernées ont été vérifiées une à une — sur
l'US, l'expression calculée redonne exactement le littéral d'origine.

Une seule exception assumée : `CameraCodePatches[37]` est un `lui` orphelin dont
la moitié basse est fournie par le code du jeu vers lequel le stub saute. Aucune
paire ici ne permet de nommer la globale visée ; les deux builds gardent leurs
globales `0x800F` dans la même page de 64 Ko, donc la moitié haute est la même.

Contrôle de non-régression : les 113 constantes migrées ont été comparées à la
colonne US de la table, **zéro divergence**.

### Le piège qui a coûté deux séances de test

Une migration qui cherche les adresses ne trouve pas les **mots d'instruction
qui en contiennent une**. Un `j` ou un `jal` commence par `0x08` ou `0x0C`, pas
par `0x8` : 21 constantes du type

```c
const uint32_t ObjectMoveJump = 0x08019B80; // j ObjectMoveStub
```

sont donc passées à travers. Le stub s'écrivait bien à son adresse Kiosk, mais
l'accroche sautait vers l'adresse **US** — le jeu partait dans du code
arbitraire. D'où un plantage qui ne se déclenchait qu'avec l'option de vitesse
des ennemis, seule à installer cette accroche‑là.

Elles sont désormais calculées par `JumpTo`/`CallTo` depuis les adresses de
stub. Restent figées, volontairement : `ObjectMoveLegacyJump` et
`ObjectMovePreviousJump`, qui servent à **détecter** un ancien patch US dans une
sauvegarde d'état, et les deux constantes Floyd dont la cible `0x80049C84` n'a
pas d'équivalent dans la table — fonctionnalité désactivée.

La même erreur s'est répétée une fois de plus, sous une autre forme : les
**déplacements** de charge et de rangement. Une paire `lui`/`lwc1` porte
l'adresse en deux moitiés, et j'avais traité les `lui` sans traiter les moitiés
basses. Le résultat était silencieux plutôt que fatal : l'hôte écrivait bien
l'élévation caméra dans son emplacement Kiosk, mais le code patché la relisait à
l'emplacement US. La caméra tournait à gauche et à droite — ce chemin‑là ne passe
pas par une paire — et ne montait ni ne descendait plus. Dix‑huit mots étaient
concernés.

La leçon est générale, et elle a coûté trois séances de test : chercher les
adresses ne suffit pas, il faut chercher **tout ce qui en encode une** — cible
de saut, moitié haute, moitié basse. Le contrôle qui les trouve toutes est de
décoder chaque mot des tables et de tester si la valeur reconstituée tombe sur
une adresse connue, plutôt que de se fier à la forme du littéral.

---


`Source/Project64-core/N64System/GameHacks/JetForceGeminiAddresses.{h,cpp}`
contient les 105 champs et les deux tables, générés depuis les données de cette
étude plutôt que recopiés. `JfgAddresses()` sélectionne la bonne table d'après
l'identifiant de la ROM chargée, et `IsSupportedRom()` s'y branche déjà.

Trois champs valent `0` en Kiosk — le saut de cinématique d'atterrissage, absent
de cette ROM. Zéro signifie « pas de cible sur ce build » et doit être traité
comme tel par l'appelant, pas patché.

**La Kiosk reste volontairement refusée** par `IsSupportedRom()`, qui exige la
table US précisément. La raison est dans le code : le reste de
`JetForceGemini.cpp` lit encore ses adresses depuis ses constantes US en dur, et
non depuis la table. Accepter la Kiosk avant d'avoir migré ces usages écrirait
chaque patch à un offset US, en plein milieu d'instructions.

`IsSupportedRom()` accepte désormais les deux ROM. Ce qui rend cette ouverture
raisonnable plutôt qu'imprudente : `CGameHackCodePatcher::SetEnabled` contrôle
toutes les entrées d'une table avant d'en écrire une seule, et renonce au
premier mot qui n'est ni l'original attendu ni un remplacement déjà posé. Une
erreur de correspondance laisse donc la fonctionnalité éteinte au lieu de
corrompre du code.

Les fonctionnalités qui sauvegardent elles-mêmes les mots d'origine et écrivent
sans contrôle échappent à ce filet : déplacement d'objets, vélocité joueur et
les accroches sidekick. Ce sont elles qu'il faut suspecter en premier si la
Kiosk se comporte mal.

**Rien de tout ceci n'a été exécuté.** Ce qui suit reste à faire :
1. **Vérifier d'abord la non-régression US.** La migration a touché tout le
   fichier ; la ROM US doit se comporter exactement comme avant. Si elle a
   bougé, le problème est dans la migration, pas dans le portage.
2. **Puis la Kiosk, une fonctionnalité à la fois.** Commencer par le 60 fps : il
   ne dépend que de `SchedulerFrameGateAdd` et de trois mots de signature, tous
   vérifiés au désassemblage, et il se constate immédiatement.
3. Ensuite la caméra et le viseur, dont les sites ont été confirmés par
   désassemblage des deux côtés.
4. En dernier les accroches qui écrivent sans contrôle de signature —
   déplacement d'objets, vélocité joueur, sidekick — puisque ce sont les seules
   où une erreur ne se signale pas d'elle-même.

Un symptôme à savoir lire : une fonctionnalité qui **ne fait rien** sur la Kiosk
est le cas béni, c'est le filet de `SetEnabled` qui a joué et l'adresse est à
revoir. Un plantage ou un comportement erratique désigne au contraire les
accroches non protégées de l'étape 4.

## Outillage

Scripts de la session, à conserver s'ils doivent resservir :
`jfgmap.py` (tables de symboles et accès ROM), `jfgsig.py` (recherche par
signature masquée). Le mapping complet est reproductible en les relançant.

## Table de correspondance

Format : `constante  adresse_US  adresse_Kiosk  méthode  note`.

```text
AnimseqCameraAddress                       801045B8 80105288  symbole           animcamera+0x0
CameraActiveOverrideBase                   800F6E58 800F7918  symbole           controlchr_gravity+0x94
CameraArrayAddress                         800FA4D0 800FAF90  symbole           controlchr_gravity+0x370C
CameraCenterBranch                         8002D154 8002D160  signature 13/13   2e candidat 5
CameraClampBranch                          8002D128 8002D134  signature 13/13   2e candidat 3
CameraFovAddress                           800FB078 800FBB38  symbole           controlchr_gravity+0x42B4
CameraHeightBlendBase                      8002E51C 8002E528  signature 13/13   2e candidat 7
CameraHeightOffsetAddress                  8009F248 8009FAD8  signature 12/13   2e candidat 8
CameraLookHelperCall                       8002E814 8002E820  signature 12/13   2e candidat 2
CameraNativeYAddress                       8009F244 8009FAD4  signature 12/13   2e candidat 9
CameraOrbitBranch                          8002DF94 8002DFA0  signature 13/13   2e candidat 3
CameraOrbitCenterBranch                    8002DED8 8002DEE4  signature 13/13   2e candidat 5
CameraOrbitGateBranch                      8002DEC8 8002DED4  signature 13/13   2e candidat 5
CameraPitchHelperCall                      8002EA5C 8002EA68  signature 12/13   2e candidat 2
CameraPositionXBaseCall                    8002E0A8 8002E0B4  signature 12/13   2e candidat 2
CameraPositionZBaseCall                    8002E0E0 8002E0EC  signature 12/13   2e candidat 2
CameraTopDownCounterAddress                8009F24C 8009FADC  signature 12/13   2e candidat 7
CameraTopDownHelperBase                    80098D08 80098CC8  signature 12/13   2e candidat 5
CameraYawHelperCall                        8002EA34 8002EA40  signature 12/13   2e candidat 2
ControlCameraAddress                       800F6DC0 800F7880  symbole           controlcam+0x0
CurrentScreenAddress                       800FECB0 800FF990  symbole           currentScreen+0x0
DisableJoyAddress                          800F6DBC 800F787C  symbole           disablejoy+0x0
DroneLateralDragAddress                    8009FCC0 800A0550  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralFlagsAddress                   8009FCE4 800A0574  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralForwardSpeedAddress            8009FCC4 800A0554  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralHookFlagsAddress               8009FCEC 800A057C  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralHookHitsAddress                8009FCE8 800A0578  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralMaxSpeedAddress                8009FCA4 800A0534  signature 12/13   2e candidat 5
DroneLateralPreviousObjectAddress          8009FCD8 800A0568  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralPreviousXAddress               8009FCD0 800A0560  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralPreviousZAddress               8009FCD4 800A0564  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralRightXAddress                  8009FCC8 800A0558  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralRightZAddress                  8009FCB0 800A0540  cave              scratch du hack, bourrage equivalent (308 o)
DroneLateralSideFactorAddress              8009FCA8 800A0538  signature 12/13   2e candidat 6
DroneLateralVelocityAddress                8009FCAC 800A053C  cave              scratch du hack, bourrage equivalent (308 o)
EnemyHalveFlagAddress                      8009FCE0 800A0570  cave              scratch du hack, bourrage equivalent (308 o)
FloydCameraPreviousObjectAddress           8009FCB8 800A0548  cave              scratch du hack, bourrage equivalent (308 o)
FloydCameraPreviousXAddress                8009FCB0 800A0540  cave              scratch du hack, bourrage equivalent (308 o)
FloydCameraPreviousZAddress                8009FCB4 800A0544  cave              scratch du hack, bourrage equivalent (308 o)
FramePacing60Branch                        800550F8 80055BDC  signature 12/13   2e candidat 2
FramePacing60SignatureBase                 800550F0 80055BD4  signature 12/13   2e candidat 2
FramePacingEscalateStore                   800550E8 80055BCC  signature 12/13   2e candidat 2
FramePacingSignatureBase                   800550DC 80055BC0  signature 12/13   2e candidat 2
GeneralRenderListAddress                   800F2FB0 800F3A70  symbole           generalBuffer+0x0
LandingCinematicSkipInputAddress           8009FCBC 800A054C  cave              scratch du hack, bourrage equivalent (308 o)
LobbyCameraInUseAddress                    800FB080 800FBB40  symbole           controlchr_gravity+0x42BC
ManualAimXVelocityStore                    8003AF14 8003ACDC  signature 13/13   2e candidat 3
ManualAimYVelocityStore                    8003AF2C 8003ACF4  signature 13/13   2e candidat 4
ObjectMoveEntry                            80009A24 800099FC  signature 12/13   2e candidat 4
ObjectMoveResume                           80009A28 80009A00  signature 12/13   2e candidat 3
OverlayTableAddress                        800FEAA0 800FF780  symbole           PatrolNodes+0x4
PlayerCountAddress                         800F2D10 800F3910  symbole           playerlist+0x4
PlayerListAddress                          800F2D0C 800F390C  symbole           playerlist+0x0
RobotMissionAddress                        800A3208 800A3A58  signature 13/13   2e candidat 8
MultiplayerGameAddress                     800A4FC4 800A5994  lbu sidekickControl US+0x58 / Kiosk+0x58
CooperativeGameAddress                     800A4FC8 800A5998  lbu sidekickControl US+0x68 / Kiosk+0x68, sb +0x90/+0xBC
SidekickControlEnd                         8003109C 800310A8  signature 12/13   2e candidat 2
SidekickControlEntry                       8002F728 8002F734  signature 13/13   2e candidat 2
SidekickControlObjectAddress               8009FCA0 800A0530  signature 12/13   2e candidat 4
SidekickControlProbeEntry                  8002F8AC 8002F8B8  signature 13/13   2e candidat 2
SidekickControlProbeStub                   80066D00 800676D0  signature 12/13   2e candidat 4
SidekickLateralMoveInputLegacyEntry        8002F8F4 8002F900  signature 12/13   2e candidat 3
SidekickPadProbeActorAddress               8009FCFC 800A058C  cave              scratch du hack, bourrage equivalent (308 o)
SidekickPadProbeObjectAddress              8009FCDC 800A056C  cave              scratch du hack, bourrage equivalent (308 o)
SidekickPadProbeStateAddress               8009FCCC 800A055C  cave              scratch du hack, bourrage equivalent (308 o)
SidekickStrafeDelay                        80030064 80030070  signature 13/13   2e candidat 2
SidekickStrafeEntry                        80030060 8003006C  signature 13/13   2e candidat 2
SidekickVelocityLateralDelay               8002F8B0 8002F8BC  signature 13/13   2e candidat 3
SidekickVelocityLateralEntry               8002F8AC 8002F8B8  signature 13/13   2e candidat 2
SidekickVelocityLateralResume              8002F8B4 8002F8C0  signature 13/13   2e candidat 3
SquaddieXStub                              8009FD60 800A05F0  cave              scratch du hack, bourrage equivalent (308 o)
SquaddieZStub                              8009FD80 800A0610  cave              scratch du hack, bourrage equivalent (308 o)
StaticCameraInUseAddress                   800FB084 800FBB44  symbole           controlchr_gravity+0x42C0
TripleBufferActive                         800FECA6 800FF986  symbole           PatrolNodes+0x20A
TripleBufferRequest                        800551E8 80055CCC  signature 12/13   2e candidat 4
WaterWakeCullingEntry                      800146E4 800144A8  signature 12/13   2e candidat 3
WaterWakeDrawFallbackCalledAddress         8009FCF0 800A0580  cave              scratch du hack, bourrage equivalent (308 o)
WaterWakeDrawFallbackStub                  8009FD00 800A0590  cave              scratch du hack, bourrage equivalent (308 o)
WaterWakeFrameRateEntry                    8006B1A0 8006B4E4  signature 13/13   2e candidat 5
WaterWakeGateCounter                       8009FCEC 800A057C  cave              scratch du hack, bourrage equivalent (308 o)
WaterWakeGateStub                          8009FD00 800A0590  cave              scratch du hack, bourrage equivalent (308 o)
WaterWakeGlobalFadeAddress                 800A6950 800A7310  signature 13/13   2e candidat 4
WaterWakeLegacyCallSite                    80009278 80009260  signature 12/13   2e candidat 5
WaterWakeObjectCountAddress                800F2CA8 800F38A8  symbole           ObjList+0x4
WaterWakeObjectListAddress                 800F2CA4 800F38A4  symbole           ObjList+0x0
WaterWakeRingRateEntry                     8006AAC8 8006AE0C  signature 13/13   2e candidat 3
WaterWakeStockDrawCalledAddress            8009FCF8 800A0588  cave              scratch du hack, bourrage equivalent (308 o)
WaterWakeStockDrawTargetAddress            8009FCF4 800A0584  cave              scratch du hack, bourrage equivalent (308 o)
WaterWakeUpdate                            8006B090 8006B3D4  signature 13/13   2e candidat 6
```
