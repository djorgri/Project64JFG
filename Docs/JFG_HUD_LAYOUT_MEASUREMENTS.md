# Marges et centrage du HUD de Jet Force Gemini

Analyse du 8 septembre 2026, ROM USA commerciale, HUD solo normal. Les captures
fournies sont recadrées : les marges ci-dessous proviennent des coordonnées du
jeu, et non des distances aux bords des captures. Aucun placement n'a été modifié
pendant l'analyse initiale. L'option ajoutée ensuite est décrite en fin de document.

## Repère et résultat d'origine

Les valeurs sont des **unités logiques sur une image de 320 × 240**, avant
rastérisation, filtrage, éventuel rognage du plugin et agrandissement à l'écran.
L'origine écran est en haut à gauche. Le repère orthographique du HUD est centré,
avec Y vers le haut : `x écran = 160 + x HUD`, `y écran = 120 - y HUD`.

| Mesure | Valeur d'origine |
| --- | ---: |
| Marge gauche du cadre principal d'arme | 19 |
| Marge haute du cadre principal d'arme | 13 |
| Marge gauche de l'enveloppe des sommets de l'arc de vie | 24 |
| Marge basse de cette enveloppe | 13 |
| Point de placement de l'icône de vie | (60, 188) |
| Centre mathématique de l'arc de vie | (61, 190) |
| Icône moins centre de l'arc | (-1, -2) |

Les marges haute et basse sont donc identiques dans cette géométrie. Les deux
éléments ne partagent pas le même bord gauche : l'arc commence **5 unités plus
à droite**. La marge gauche du cadre dépasse sa marge haute de **6 unités**.

Le point de placement de l'icône est **1 unité à gauche et 2 au-dessus** du
centre de l'arc. Ce décalage est inscrit dans le code original. Il ne faut pas
confondre ce point de placement avec le centre apparent de l'orbe verte ou le
centre de tous les pixels opaques du sprite, dont le dessin est asymétrique.

Ces écarts entiers comparent les **coordonnées de placement avant projection**.
Le rendu des sprites ajoute le W de leur matrice locale au W de leur ancre ;
l'écart final à l'écran n'est donc pas exactement `(-1,-2)`. La correction
décrite ci-dessous tient aussi compte de cette division homogène.

## Preuves dans le jeu

### Point de placement de l'icône

`setupFrontEndObject`, à `0x8005A048`, copie des enregistrements de 32 octets de
la table `0x800A51DC` vers les objets temporaires à `0x800FF780`. L'objet 2,
dessiné par `frontDrawObj(2)`, a sa définition d'origine à `0x800A521C`
(offset `0xA5E1C` dans la ROM normalisée en ordre big-endian).

Les flottants à `+0x08`, `+0x0C`, `+0x10` et `+0x14` valent respectivement
`1`, `-100`, `-68`, `0` : échelle, X, Y et Z. Le point écran vaut donc
`(160 - 100, 120 + 68) = (60, 188)`.

### Centre et sommets de l'arc

Dans `instDrawHealth`, overlay 6, les instructions `+0x3C8` à `+0x3F4` lisent
les coordonnées de cet objet et appellent `matrixTranslate` avec
`X + 1` et `Y - 2`. Le centre de l'arc est donc `(-99, -70)` dans le repère
HUD, soit `(61, 190)` dans le repère écran.

La construction des sommets, à partir de l'overlay 6 `+0xC74`, utilise les
rayons théoriques **38 extérieur** et **20 intérieur** en solo. Elle avance par
pas angulaires `0x1555` (environ 30 degrés) et tronque les coordonnées vers zéro.
Les sommets extérieurs générés, contrôlés dans la mémoire d'une sauvegarde,
ont pour extrema locaux `X = -37..37`, `Y = -37..38`.

L'enveloppe de ces sommets est donc `x = 24..98`, `y = 152..227` à l'écran.
Les secteurs effectivement visibles dépendent de l'état de la jauge ; les
captures contiennent les secteurs qui atteignent les extrema gauche et bas.
La marge basse géométrique vaut `240 - 227 = 13`.

Un cercle idéal de rayon 38 donnerait des marges de 23 à gauche et 12 en bas.
Ce sont les **sommets tronqués du jeu**, et non cette approximation circulaire,
qui donnent les marges 24 et 13 ci-dessus. Le centre mathématique reste (61,190),
même si le centre d'une boîte englobante diffère légèrement, ou si l'arc est
incomplet.

### Cadre principal d'arme

L'overlay 14 charge la translation `(-141, +107)` aux offsets `+0x264C` à
`+0x2664`. Son tableau de 20 sommets à `+0x3E80` possède les extrema locaux
`X = 0..59`, `Y = -59..0`. La translation écran du coin supérieur gauche est
donc `(160 - 141, 120 - 107) = (19, 13)` ; l'enveloppe est
`x = 19..78`, `y = 13..72`.

Cette mesure porte sur le cadre principal, pas sur chaque excroissance, sprite
animé, lueur filtrée ou élément du sélecteur d'armes.

## Comparaison des formats d'affichage

En basse résolution, les modes 4/3 et widescreen utilisent tous deux un tampon
de 320 × 240 pour ces coordonnées HUD. La table vidéo du jeu distingue le
traitement widescreen de la scène. Présenter le HUD d'origine sur une surface
16/9 étire ses distances horizontales par rapport aux verticales.

Pour ces éléments ancrés à gauche, le patch actuel applique
`x corrigé = 0,75 × x original + 4`, en coordonnées du tampon ; Y est conservé.
Il donne ainsi une marge gauche de **18,25** pour le cadre et **22** pour l'arc.
Ces nombres ne doivent pas être comparés directement aux marges verticales :
les pixels du tampon sont ensuite présentés sur une image 16/9.

À **hauteur d'image égale de 1080 pixels**, sans rognage, on obtient :

| Mesure en pixels d'affichage, avant filtrage | 4/3 d'origine, 1440 × 1080 | 16/9 d'origine, 1920 × 1080 | 16/9 avec correction, 1920 × 1080 |
| --- | ---: | ---: | ---: |
| Marge haute du cadre | 58,5 | 58,5 | 58,5 |
| Marge gauche du cadre | 85,5 | 114 | 109,5 |
| Marge gauche de l'arc | 108 | 144 | 132 |
| Marge basse de l'arc | 58,5 | 58,5 | 58,5 |
| Décalage horizontal icône / centre de l'arc | -4,5 | -6 | -4,5 |
| Décalage vertical icône / centre de l'arc | -9 | -9 | -9 |

Le décalage relatif de l'icône existe donc déjà à l'origine. La correction
actuelle en conserve les proportions après présentation en 16/9. Son choix
d'ancrage ajoute toutefois **24 pixels de marge gauche à hauteur 1080**, par
rapport au 4/3 à la même hauteur, pour ces deux éléments. Ce déplacement global
est distinct des différences de placement internes du HUD d'origine.

## Limites

Ces valeurs décrivent le HUD solo et sa géométrie de base, pas les marges d'une
fenêtre recadrée ni le multijoueur. Une mesure des pixels visibles peut varier
avec le filtrage, la transparence, l'animation et les arrondis de rendu. La
haute résolution n'a pas fait l'objet d'une comparaison complète ici.

La vérification visuelle du centre des segments des captures concorde avec un
léger placement de l'ensemble de l'icône au-dessus de l'arc. Elle ne remplace
pas les coordonnées ROM : en particulier, l'orbe verte seule est plus haute
que le centre de l'ensemble du dessin.

## Option « Align HUD elements »

L'option indépendante du dialogue *Game-specific hacks* cible le HUD solo de
la ROM USA commerciale, en modes 0/1/2/3. Elle fonctionne avec ou sans
*Correct widescreen HUD* ; seule cette seconde option corrige les proportions.
Le choix retenu est **la même marge gauche de 13** pour le cadre et l'arc,
plutôt qu'un alignement de leurs centres horizontaux. Leurs dimensions restent
différentes. La marge est exprimée dans le repère logique 320×240, à hauteur
d'affichage égale et avec la présentation 4/3 ou 16/9 correspondant au jeu.

| Cible dans le framebuffer | Mode 0 (320×240, 4/3) | Mode 1 (320×240, WS) | Mode 2 (448×336, 4/3) | Mode 3 (448×336, WS) |
| --- | ---: | ---: | ---: | ---: |
| Gauche du cadre, bordure VI compensée en widescreen | 13 | 13,75 | 18,25 | 19,25 |
| Gauche géométrique de l'arc, compensations incluses | 13 | 12,25 | 18,25 | 17,25 |
| Haut du cadre / bas de l'arc | 13 | 13 | 18,25 | 18,25 |

Les cibles haute résolution sont arrondies au quart de pixel du framebuffer,
pour utiliser la grille 10.2 des rectangles RDP. Pour la cible de base de 13,
l'erreur est inférieure à 0,1 unité logique. Le VI comprime le tampon entier
en widescreen : il ne faut
pas soustraire arbitrairement 30 ou 42 lignes aux coordonnées du HUD.

En mode 0, le cadre et ses éléments reçoivent une translation écran de **−6**
en X, l'arc de **−11**, sans déplacement vertical de ces deux éléments. En
mode 1 avec correction de proportions, les déplacements valent **−4,5** et
**−9,75** pixels du framebuffer, compensations visuelle et VI incluses.
Les autres modes sont calculés depuis les
mêmes coordonnées ROM, le tampon et les biais widescreen 48/68.

Les captures de validation montrent que le bord visible de l'arc reste un peu
en retrait malgré l'égalité des marges géométriques. À la demande de l'utilisateur,
une compensation visuelle de **−2 unités logiques** est ajoutée à l'ensemble
arc + icône, uniquement dans les modes widescreen 1/3. Convertie dans le tampon
et arrondie au quart de pixel, elle vaut **−1,5** en basse résolution et **−2**
en haute résolution. La marge géométrique de l'arc, mesurée après la bordure
masquée du VI, est donc d'environ 11 unités, pour rapprocher son bord apparent
de celui du cadre. Ce réglage empirique
conserve le centrage, la hauteur et le placement en 4/3 ; il appartient à
*Align HUD elements* et ne dépend pas de la case de correction des proportions.

### Bordure VI et marges visibles — 9 septembre 2026

La capture après la compensation de l'arc montre des bords gauches désormais
alignés, mais une marge gauche d'environ **23 pixels visibles**, contre
**38 pixels en haut et en bas**. Le calcul précédent partait du bord du tampon,
sans tenir compte de la bordure noire produite par le circuit vidéo.

Dans `external/parallel-rdp/parallel-rdp/video_interface.cpp`, `analyze_line`
fixe `h_start_clamp = h_start + 8` et `h_end_clamp = h_end - 7` pour le mode
normal ; `vi_scale.frag` masque les pixels hors de cette plage. Cette bordure
existe même lorsque `OverscanCrop=0`. Le viewport du HUD, vérifié dans la ROM
et une sauvegarde, reste exactement centré : aucun décalage de −4 n'y est ajouté.

En basse résolution, les huit échantillons VI masqués correspondent à **4 pixels
du tampon**. Sur cette capture à un facteur horizontal de 4, la marge prévue
de `9,75` devient donc `(9,75 − 4) × 4 = 23` pixels visibles. La correction
ajoute **+4 pixels framebuffer** à tout le groupe d'armes et à toute la vie,
uniquement en widescreen : la marge visible attendue devient **39 pixels**.
Le bandeau, ses textes, les compteurs et la sélection suivent le cadre ;
le centrage de la vie et la compensation relative de son arc sont conservés.

En haute résolution, le registre XScale du jeu vaut `(448 << 9) / 320 = 716`.
La bordure équivaut donc à `8 × 716 / 1024 = 5,59375` pixels framebuffer,
arrondis à **5,5** pour rester sur la grille commune. Cette correction ne
modifie ni le plugin vidéo ni le placement 4/3 déjà approuvé. Elle vise le
mode VI standard de JFG avec ParaLLEl-RDP ; un recadrage supplémentaire choisi
dans le plugin peut encore modifier les marges visibles.

Le pivot de l'icône est placé au centre mathématique de l'arc **après projection**.
Si `C = largeur/2`, `S = 0,75` avec correction widescreen (sinon 1), et `B` vaut
48/68 avec correction (sinon 0), le décalage complémentaire par rapport à la
translation de l'arc est :

- `dx = S × (C − 99 − B) / (C + 1)` ;
- `dy = 70 − 68 × C / (C + 1)` dans le repère écran Y vers le bas.

Cela compense le W du billboard : `C+1` pour l'icône, `C` pour l'arc. Le
centrage concerne le **pivot du sprite**, pas le barycentre des pixels opaques
ou de l'orbe verte. La texture garde son dessin, sa rotation et ses animations.

### Mise en œuvre et restauration

Les appels locaux de dessin choisissent la translation du groupe d'armes ou
de la vie. Au dernier `mathMtxF2L`, les positions des matrices sont translatées
temporairement, puis leurs sources sont restaurées à l'identique. Le W ajouté
par le billboard est pris en compte pour les sprites. Les coordonnées des
objets, sommets et animations ne sont jamais réécrites.

Le wrapper autour de l'appel `overlay14+0xC9C → +0x292C` translate les
rectangles de texte, les deux compteurs de munitions, les jauges et les zones
de découpage émis par le groupe d'armes. Le bandeau de ramassage, sa fermeture
et le sélecteur suivent le même déplacement que le cadre. Les rétablissements
du découpage plein écran sont conservés. Les statistiques de région et les
formes 3D sont hors de ce périmètre.

Les stubs occupent `[0x800679A0,0x800680A0)`, séparés des caves widescreen,
avec un octet de portée distinct à `0x80102552`. Cette plage appartient au
dessin des registres de l'écran de panne du jeu : son entrée `0x80067994`
retourne immédiatement pendant l'activation. **L'affichage de ces registres
sur l'écran de panne est donc indisponible lorsque l'option est active** ;
le journal d'exception et l'initialisation du jeu sont conservés. Les 448 mots
originaux et l'entrée sont intégralement restaurés après retrait de tous les
appels, y compris lors de l'adoption d'une sauvegarde déjà patchée.

Les tests exécutent les stubs MIPS et le véritable installateur C++ : géométrie,
transfert des arguments, portée imbriquée, restauration des matrices,
rectangles/découpage, cases indépendantes, résolutions, sauvegardes, overlays
relocalisés et refus des signatures étrangères. L'utilisateur a validé le
rendu de base en 4/3 et 16/9, puis un bandeau de ramassage en 16/9 (texte et
fermeture raccordés au cadre). La compensation relative de l'arc a ensuite
rapproché les bords gauches ; la nouvelle compensation commune de la bordure VI,
le sélecteur en mouvement et la haute résolution restent à comparer dans le
jeu ; ces tests ne prouvent pas la lisibilité de chaque pixel filtré.
