# État du prototype 16/9 de Jet Force Gemini

État au 9 septembre 2026. La case **Correct widescreen HUD** active le prototype.
L'utilisateur a validé le placement et les proportions du compteur de munitions,
avec une légère perte de netteté acceptée. La correction des segments du
réticule a également été validée visuellement par l'utilisateur sur son essai
du 8 septembre. Les formes 3D, déjà correctes, restent intactes. L'utilisateur
a également validé le bandeau de ramassage affichant **Gemini Capacity Increased**.
Le dernier réglage des barres vertes et leur changement de format sont couverts
par les tests automatiques ; leur validation visuelle reste en attente.
Les observations historiques de Claude sont distinguées des mesures de reprise.

Les [mesures des marges et du centrage du HUD d'origine](./JFG_HUD_LAYOUT_MEASUREMENTS.md)
proviennent des coordonnées ROM et des sommets du jeu : marges haute/basse de
13 unités, marges gauches de 19 pour le cadre d'arme et 24 pour l'arc de vie,
et pivot de l'icône décalé de 1 unité à gauche et 2 vers le haut par rapport
au centre de l'arc, dans le repère logique 320 × 240.

## Activer le prototype

1. Utiliser la ROM USA commerciale prise en charge par le projet. Le patch HUD
   exclut explicitement la version Kiosk, même si d'autres adaptations la gèrent.
2. Sélectionner le mode widescreen dans les options du jeu lui-même.
3. Dans les paramètres du plugin **Project64 Parallel RDP**, activer
   **Force 16:9 display (stretches image)** pour présenter l'image en 16/9.
4. Dans **Options → Game-specific hacks**, cocher **Correct widescreen HUD**,
   puis reprendre une partie pour observer la correction.

Ces réglages sont indépendants : le plugin choisit le format de présentation,
le jeu choisit son mode vidéo et le hack corrige certains dessins du HUD. La
case HUD n'active pas le mode vidéo du jeu. Elle ne nécessite pas les commandes
clavier/souris et reste désactivée par défaut.

## Activation limitée au widescreen du jeu

Le contrôle utilise l'octet du **mode vidéo actif** à `0x800FECA8`, écrit par
`viChangeMode` : `0` et `2` correspondent au 4/3, `1` et `3` au widescreen
basse et haute résolution de la ROM USA. La préférence du menu n'est pas
utilisée comme substitut au mode actif. Le format forcé dans le plugin
graphique n'entre pas dans cette décision.

L'installateur vérifie lui-même que le mode vaut `1` ou `3`, en plus du
contrôle effectué à chaque image. Revenir en 4/3 demande le retrait de tous
les appels corrigés ainsi que des constantes du bandeau. Les coordonnées natives
des jauges restent désormais intactes pendant le dessin. La
case peut donc rester cochée lorsque le jeu est en 4/3. Les routines MIPS de
dessin possèdent également leur propre garde sur le bit widescreen.

Le chargement d'une ancienne sauvegarde contenant les signatures connues du
prototype déclenche désormais leur récupération et leur retrait même en
4/3 ou avec la case décochée. Les overlays actuellement chargés sont vérifiés,
y compris lorsqu'ils ont changé d'adresse. Une signature invalide du réticule
n'empêche plus le nettoyage indépendant du bandeau et des jauges ; les zones
de code restent disponibles tant qu'un appel reconnu pourrait encore y mener.
Le diagnostic de portée forcée n'est plus réarmé pendant un retrait.
Les quatre anciennes corrections numériques, abandonnées pendant la recherche
du compteur de munitions, sont également retirées lorsqu'elles subsistent
seules dans une sauvegarde, sans installation HUD reconnue.

Les tests exécutent les méthodes C++ réelles contre une mémoire simulée pour
vérifier ces transitions et chargements. Les tests MIPS comparent également
les sorties des routines au comportement d'origine en modes `0` et `2`, même
avec une portée HUD active. Ils ne remplacent pas un essai visuel du passage
4/3 ↔ 16/9 dans l'émulateur.

## Architecture existante

Le travail se trouve principalement dans
[`JetForceGemini.cpp`](../Source/Project64-core/N64System/GameHacks/JetForceGemini.cpp),
dans les constantes `WidescreenHud*`, `ProcessRuntimeFrame()` et
`PatchWidescreenHud()`.

Le patch attend un index de résolution égal à `1` ou `3`, un joueur et une caméra valides,
`DisableJoy == 0` et la signature reconnue de l'overlay 14. Il installe des
instructions MIPS dans une zone de diagnostic devenue inutilisée pendant le
jeu, de `0x80067280` à `0x80067690` exclu. Cette installation tardive évite
d'écraser du code encore exécuté au démarrage. L'extension pour le compteur
occupe le début de `diCpuReportWatchpoint`, une fonction de diagnostic de panne
inutilisée en partie, et s'arrête avant `diCpuLogMessage` à `0x800676B4`.
Le réticule et les deux nouveaux stubs des jauges utilisent un second segment
séparé, de `0x80067790` à `0x80067844` exclu, dans l'ancien affichage du journal
de diagnostic. Le code du réticule se termine toujours à `0x800677F4` ; le wrapper
des jauges commence à cette adresse et son helper d'ancrage à `0x80067810`.
La fonction diagnostic suivante commence à `0x800678C4` et reste intacte. L'unique appelant
à `0x800674BC` est déjà remplacé par la cave HUD. L'image mémoire concatène les
deux segments : le code entre eux, notamment `diCpuLogMessage`, n'est ni lu ni
écrit par l'installateur de cave.

La plupart des corrections s'appliquent entre l'entrée et la sortie du dessin
`frontSingleInstruments`, aux offsets `+0xC00` et `+0x10D0` de l'overlay 14.
Un octet à `0x80102553` indique si cette portion du HUD est en cours de dessin.
Le réticule est dessiné avant l'entrée : ses segments disposent désormais de
sept appels corrigés distincts dans l'overlay 13. Les polices texturées consultent seulement le mode vidéo une fois
leurs hooks installés.

Le prototype de correction place quinze stubs : deux pour ouvrir/fermer la portée,
dix pour le rendu HUD, un pour les segments du réticule et deux pour identifier
et ancrer le dessin des jauges. Les dix corrections HUD
vérifient toutes le bit widescreen ; huit vérifient aussi la portée.
Le stub du réticule vérifie le mode widescreen et l'absence de portée HUD, afin
d'éviter une double correction lorsque le diagnostic O force cette portée.
Les deux stubs de police texturée sont volontairement globaux
pendant le mode widescreen. Le garde manquant évoqué dans le compte rendu
n'a donc pas été retrouvé dans ces chemins actuels. Le helper des barres de
tir s'exécute seulement après les gardes de mode et de portée du stub de rectangles.

| Élément ou chemin de dessin | Correction présente dans le code |
| --- | --- |
| Matrice orthographique du HUD | Échelle horizontale multipliée par 0,75. |
| Sprites et icônes d'armes | Deux chemins de `camDo2DSprite` corrigés en largeur, avec compensation de position gauche/droite. |
| Cadres et jauges utilisant des matrices | Compensation de l'ancrage dans `matrixTranslate`. |
| Police texturée | Agrandissement vertical proche de 21/16 et pas de texture de 3/4 pour préserver les colonnes fines des glyphes. |
| Lignes, radar et petite police en traits | Coordonnées X transformées à la mise en file, avant le dessin différé. |
| Rectangles pleins | Compression horizontale avant émission des commandes de dessin. |
| Six barres vertes de capacité de tir | Ancrage du cadre d'arme appliqué aux rectangles pendant leur dessin ; table native conservée, gardes de mode et de portée. |
| Chiffres du compteur de munitions | `frontPrintNum` : compression horizontale des glyphes et de leur espacement, ancrage sur le panneau gauche et adaptation du pas de texture ; premier rendu validé en interpréteur. |
| Bandeau de ramassage | Extrémité attachée à gauche pendant toute l'animation, texte centré à pleine ouverture et découpe alignée sur le bandeau ; affichage complet validé visuellement par l'utilisateur. |
| Segments des réticules | Compression horizontale autour du point visé, avant découpe et dessin CPU ; arrondi symétrique et formes 3D intactes. Rendu validé par l'utilisateur sur son essai du 8 septembre. |

L'installateur vérifie les instructions originales et les limites de la zone
des stubs. Il gère aussi le retrait des hooks, les changements d'overlay et
certains patches hérités des anciennes sauvegardes.

## Barres vertes de capacité de tir

L'ancien patch soustrayait 45 aux coordonnées X des sept rectangles de
l'overlay 14 : six barres et leur fond. Leur ancrage en basse résolution
restait environ 2,25 pixels à droite de celui du cadre. Si le mode widescreen
ou la portée HUD se désactivait avant le retrait côté émulateur, le renderer
lisait encore ces coordonnées déplacées avec son comportement 4/3. Ce décalage
vers la gauche est reproduit par les tests de transition.

L'appel à `frontDrawRectangles` à `overlay14 +0x2B28` passe maintenant par un
wrapper dédié. Son adresse de retour fixe permet au stub de rectangles de
reconnaître les jauges, sans modifier leur table ni les autres appels de rectangles.
Après les gardes de mode et de portée, l'ancrage devient
`X' = floor(0,75 × X + 4)` en basse résolution et
`X' = floor(0,75 × X + 53)` en haute résolution. Ces formules suivent le cadre
d'arme, dont l'origine est exprimée dans le repère de la matrice, tandis que
la table des jauges conserve ses coordonnées de référence 320 pixels.
La translation optionnelle **Align HUD elements** s'applique ensuite à l'ensemble.

Les anciennes sauvegardes sont migrées en restaurant seulement les X reconnus
comme natifs ou décalés de −45, y compris un mélange de ces deux variantes.
Les Y et couleurs du jeu sont préservés. Un X inconnu empêche toute migration
de la table ; l'installateur ne remplace pas arbitrairement ces données.

Les tests exécutent les stubs MIPS réels pour les sept rectangles, les deux
résolutions widescreen et les changements de mode/portée avant retrait des
hooks. Ils vérifient aussi les autres rectangles, les arguments du wrapper
et l'absence d'écriture dans la table. Les tests C++ couvrent l'installation,
le retrait et la migration. Le 9 septembre, les 67 tests JFG passent et la
compilation Release x64 termine sans erreur ni avertissement.
La comparaison visuelle de ce dernier réglage,
notamment lors des bascules 4/3 ↔ 16/9, reste à effectuer.

## Segments des réticules

L'utilisateur a confirmé le bon résultat visuel après l'essai de la version
corrigée avec sa sauvegarde `reticle.pj.zip`. Cette validation ne couvre pas
encore explicitement toutes les armes ni la haute résolution.

`frontPlayerTarget` projette le point visé, puis `frontDrawTarget` (overlay 13,
offset `+0x4A8`) dessine les réticules. Les segments ordinaires passent par
`fxDrawLineInWindow` avant leur mise en file et leur dessin CPU dans le
framebuffer. Ils étaient hors de la portée HUD et conservaient donc leur
largeur 4/3 à l'affichage 16/9.

Sept appels des types de segment 0 à 4 passent maintenant par le nouveau stub,
qui transforme chaque extrémité : `X' = centreX + arrondi(0,75 × (X − centreX))`.
L'arrondi des demi-entiers s'éloigne de zéro pour que les côtés restent
symétriques. Le centre projeté est conservé même lorsque la visée est décentrée.
La correction intervient après les rotations/symétries et avant le découpage ;
le moteur de lignes conserve son trait natif. Les coordonnées Y, couleurs,
arguments de pile et calculs de collision/visée restent inchangés.

Les quatre appels des cadres ancrés aux bords (types 5/6) et les petites formes
3D restent intacts. Il ne faut pas appliquer la compression à la projection 3D
du réticule, que l'utilisateur a validée. Le mode 4/3 et la désactivation de
**Correct widescreen HUD** conservent les appels originaux.

L'installateur vérifie le module 13 actif, le prologue de `frontDrawTarget`, les
sept appels et leurs instructions suivantes. Les appels sont installés après
la cave et retirés avant sa restauration. Un module déchargé ou déplacé est
oublié sans écriture dans son ancienne allocation. Les signatures ont été
vérifiées dans la sauvegarde utilisateur `Save/reticle.pj.zip`.

Pour le test visuel, charger cette sauvegarde avec la case cochée, déplacer la
visée horizontalement et verticalement, puis comparer case décochée. Vérifier
la symétrie, le point visé, les changements d'arme et la haute résolution. Ne
pas utiliser le diagnostic O pour cette comparaison.

Le script [`jfg-reticle-trace.js`](../Source/Script/jfg-reticle-trace.js), à lancer
manuellement avec le cœur Interpreter, compare les coordonnées à l'entrée du
stub et à celle du moteur de découpe. Il écrit au maximum 512 relevés distincts
dans `JfgReticleTrace.log` près de l'exécutable, sans modifier la mémoire ni les
registres du jeu. Les tests automatiques du réticule couvrent 11 920 scénarios
(modes, portée, centre décentré, arrondis, symétrie, arguments et découpe) ; ils
ne remplacent pas cette observation du rendu.

## Bandeau de ramassage

La trace du ramassage **Gemini Capacity Increased** confirme que le fond du
bandeau passe par `matrixTranslate`, l'extrémité par `frontDrawObj(5)` puis
`camDo2DSprite`, et les deux passes du texte par `fontPrintXY` dans l'overlay 14.
L'objet 5 se déplace de X = −78 à 122. L'ancienne classification gauche/centre/
droite changeait son ancrage pendant ce trajet, jusqu'à ajouter 72 pixels de
séparation en basse résolution par rapport au fond resté attaché à gauche.

Le stub de position reconnaît cet objet à `v1 = 0x800FF820` et lui conserve
l'ancrage gauche. Il occupe 30 mots dans son emplacement de 32 mots. Le choix
de biais en haute résolution est également corrigé : ±68 n'est plus écrasé par
±48 dans les chemins gauche/droite.

Douze instructions de l'overlay 14 corrigent le texte et sa découpe sans
agrandir la cave. Le texte conserve ses glyphes et coulisse à la même vitesse
que l'extrémité : `X = 0,75 × capX + 46` en 320 pixels, `+95` en 448 pixels.
L'alignement horizontal centré place le message au milieu du fond entièrement
ouvert. Le décalage d'un pixel entre les deux passes et les coordonnées Y
restent inchangés. La largeur de **Gemini Capacity Increased**, calculée depuis
la conversion de caractères et la police 2 de la ROM USA, est de 136 pixels.

La découpe suit le même ancrage : `[65,5 ; 0,75 × capX + 124]` en basse résolution
et `[114,5 ; 0,75 × capX + 173]` en haute résolution. Elle passe d'une largeur
nulle à 150 pixels pendant l'ouverture. Les signatures originales des douze
instructions ont été vérifiées dans l'overlay USA chargé. L'installateur
reconnaît les variantes des deux résolutions lors du changement de mode ou du
retrait ; il restaure les instructions originales à la désactivation et ne
touche pas à une ancienne allocation après le déchargement de l'overlay.
Le retrait reconnaît aussi l'ancienne version complète du stub de position
(27 instructions), pour qu'une sauvegarde expérimentale ne réintroduise pas
l'ancien ancrage pendant qu'une installation plus récente est déjà active.

Les tests d'instructions du HUD et de son cycle d'activation se lancent avec :

```text
python -m unittest discover -s Source/Script/tests -p "test_jfg_*.py" -v
```

Les tests du cycle d'activation compilent les méthodes C++ de production avec
MSVC sous Windows (ou un compilateur C++17 disponible ailleurs). Ils sont
signalés comme ignorés si aucun compilateur n'est disponible. Aucune ROM
n'est nécessaire pour cette suite.

Les tests du bandeau exécutent les remplacements MIPS sur 9 648 scénarios :
course complète de l'extrémité, seuils gauche/centre/droite, portée, modes vidéo,
texte, ombre et découpe. Ils vérifient aussi la préservation des coordonnées Y
et de l'état de contrôle de la FPU. Ils ne remplacent pas la comparaison en jeu.

Le script de diagnostic
[`jfg-banner-trace.js`](../Source/Script/jfg-banner-trace.js) consigne ces chemins,
les coordonnées et la largeur du texte dans `JfgBannerTrace.log` près de
l'exécutable. Comme la trace du compteur, il nécessite le cœur Interpreter et
lit uniquement les registres et la mémoire. Il doit être lancé avant le
chargement de la sauvegarde. Pour la validation visuelle, reprendre la même
sauvegarde, ramasser l'objet et comparer la case activée/désactivée ; vérifier
l'ouverture, l'affichage complet et la fermeture du bandeau.

## Observations rapportées par Claude

Le cadre d'arme, les jauges, le radar, les sprites et le texte réagissaient
correctement aux corrections pendant ses essais. Le compteur de munitions
restait inchangé. La qualité du texte était légèrement inférieure à la
référence 4/3.

| Chemin testé | Observation rapportée |
| --- | --- |
| File `fxDrawDigitalNumber` | Aucun item de ce type sur environ 2 400 images ; comprimer son curseur n'a eu aucun effet visible. |
| Renderer de police `fontPrintXY` | La correction de ses `TextureRectangle` modifiait « Battle Cruiser », mais pas le compteur. |
| Six chemins sous la portée d'instruments | Forcer la portée ouverte ne modifiait pas le compteur. |
| File du texte fin | Claude avait proposé un test au pas quadruplé, sans résultat visuel recueilli. La sonde de reprise à `0x8006ED3C` n'a ensuite jamais été atteinte pendant l'essai utilisateur. |

Ces résultats orientent la reprise dans les situations observées. Ils ne
prouvent pas qu'un renderer n'est jamais utilisé par le jeu dans une autre
scène ou un autre mode.

## Compteur de munitions : renderer confirmé

La sonde de rapprochement à `0x8006ED3C` était installée, mais son témoin
persistant n'a pas été déclenché : `JfgAmmoTextProbe.log` indiquait **path NOT
SEEN after 120 runtime checks**. L'utilisateur n'observait aucun changement,
y compris en tirant et en changeant d'arme. Cette mesure écarte ce site pour
l'essai réalisé ; elle ne démontre pas que le moteur de texte fin n'est jamais
utilisé ailleurs. La case de diagnostic et sa sonde ont été retirées.

Le compteur utilise en réalité `frontPrintNum` à `0x80058EF0`. Cette fonction
émet ses propres commandes `TextureRectangle`, sans passer par le renderer
de `fontPrintXY`, par `fxDrawDigitalNumber` ou par la chaîne candidate
`fxInttostr` (`0x8006D70C`) / `fxTinyPrint` (`0x8006D60C`). Cela explique
l'absence d'effet des patches précédents, y compris celui de la police texturée.
Les quatre patches `WidescreenHudDigitalRetired` restent uniquement destinés
à restaurer les instructions d'anciennes sauvegardes.

L'analyse de l'overlay 14 a identifié deux appels à `frontPrintNum`, puis une
trace d'exécution en interpréteur les a confirmés sur une copie de la partie
affichant **96 / 100**, avec `resolution=1` et `hudScope=1` :

| Appel dans l'overlay 14 | Valeur `a0` | Coordonnées centrées `a1`, `a2` | Texture `a3` | Chiffres et couleurs |
| --- | --- | --- | --- | --- |
| `+0x2C2C`, retour `+0x2C34` | 96 (`0x60`), munitions disponibles | -99, 74 | 8 | 3 positions, jaune `FFFF00A0`, zéros atténués `FFFF0040` |
| `+0x2CA8`, retour `+0x2CB0` | 100 (`0x64`), capacité | -102, 61 | 9 | 3 positions, blanc `FFFFFFA0`, zéros atténués `FFFFFF40` |

Dans cette capture, la base de l'overlay vaut `0x80342740` : les adresses de
retour observées sont donc `0x80345374` et `0x803453F0`. La base peut changer
avec le chargement du jeu. Des commandes de dessin correspondantes sont aussi
présentes dans la RAM de la sauvegarde : la première ligne emploie des glyphes
larges de 11 pixels, espacés de 12 ; la seconde des glyphes de 8 pixels,
espacés de 8. Le moteur dessine les chiffres de droite à gauche.

### Correction ajoutée

`WidescreenHudAmmoCode` contient 32 mots MIPS dans la plage
`0x80067610..0x80067690` exclu. Cinq patches l'intègrent à `frontPrintNum` :
un saut à `0x8005900C`, deux chargements à `0x800590D0` et `0x8005921C`, et
deux suppressions d'anciens ORI à `0x800590F4` et `0x80059228`.
Les deux chemins sont couverts : chiffres significatifs et zéros atténués.

Le stub exige la portée HUD active, le bit widescreen, et une texture égale
à l'un des pointeurs des items 8 ou 9. Il multiplie la largeur de destination
et l'espacement par 0,75 ; les espacements deviennent respectivement 9 et
6 pixels. Les coordonnées de rectangles conservent leur précision au quart
de pixel. L'ancrage suit le panneau gauche : `x' = 0,75*x + 4` en mode 1
(320 pixels de large), `x' = 0,75*x + 5` en mode 3 (448 pixels).

La largeur source conservée à `stack+0x9C` continue de sélectionner les colonnes
de l'atlas : elle n'est pas comprimée. Seuls la largeur de destination,
l'espacement à `stack+0xA0` et `dsdx` changent. Le pas horizontal passe de
1024 à 1365, approximation fixe de 4/3. Les coordonnées Y, les UV de départ et
le pas vertical `dtdy=-1024` restent inchangés. La police ordinaire conserve
sa correction existante.

Le local inutilisé `stack+0x88` transporte le mot de pas de texture. Il est
initialisé à la valeur normale même lorsque les gardes refusent la correction,
puisque les deux instructions qui le chargent sont installées globalement.
L'installateur capture et restaure les mots de la cave et vérifie les signatures
des instructions. La borne de la cave empêche d'empiéter sur `diCpuLogMessage`.

Une exécution avec la correction compilée confirme les valeurs préparées avant
la boucle qui place les chiffres :

| Texture | Coordonnées X préparées en 10.2 (quart de pixel) | Largeur source | Espacement corrigé | Mot de pas de texture |
| --- | --- | --- | --- | --- |
| 8 | Gauche 199, droite 232 | 11 | 9 pixels | `0555FC00` |
| 9 | Gauche 190, droite 214 | 8 | 6 pixels | `0555FC00` |

Le pas `0555FC00` est observé dans les chemins des chiffres significatifs et
des zéros atténués. Une première capture de la scène **96 / 100**, en
interpréteur et basse résolution, montre les deux lignes comprimées dans le
cadre. Cette validation porte sur une seule scène : la lisibilité selon les
chiffres, les transitions, les changements d'arme, la haute résolution et le
cœur recompilateur doivent encore être vérifiés.

### Reproduire la trace sans modifier la RAM du jeu

Le script [`jfg-ammo-trace.js`](../Source/Script/jfg-ammo-trace.js) utilise les
événements `events.onexec` du débogueur. Il lit les registres et la RAM ; il ne
patche aucune instruction et ne modifie pas les compteurs.

1. Utiliser la ROM USA et sélectionner temporairement le cœur **Interpreter**.
   Les événements d'exécution de ce script nécessitent ce cœur.
2. Lancer le script depuis la fenêtre **Scripts** du débogueur avant de charger
   la sauvegarde de partie. Il est aussi possible de le copier dans le dossier
   `Scripts` près de l'exécutable et de l'activer au démarrage.
3. Charger la scène, observer le compteur, tirer et changer d'arme.
4. Lire `JfgAmmoRendererTrace.log` près de l'exécutable. Pour `frontPrintNum`,
   chaque ligne donne l'appelant, la valeur, les coordonnées, l'item de texture,
   les couleurs, la résolution et la profondeur de portée HUD.
5. Arrêter le script, retirer son lancement automatique s'il a été configuré,
   puis rétablir le cœur habituel pour les essais de rendu.

Le script trace aussi les anciens candidats `fxInttostr`, `fxTinyPrint`,
`tinyRender` et `tinyAdvance`. Il conserve au maximum **512 combinaisons
distinctes d'arguments**, sans compter les répétitions. Il écrit le journal
uniquement à l'initialisation et lorsqu'une nouvelle combinaison est observée.
Chaque lancement recommence le fichier : conserver une copie pour comparer
deux sessions. Une absence de ligne n'est exploitable qu'après avoir vérifié
que le script était actif en interpréteur pendant la scène concernée.

## Défauts et limites repérés à la lecture

- **Touche de diagnostic O :** elle force le compteur de périmètre à `0x20`.
  Au second appui, seul le booléen est désactivé ; l'octet n'est pas remis à
  zéro. Les entrées et sorties normales s'équilibrent et peuvent le laisser
  non nul. Le message « scope restored » ne prouve donc pas le retour au
  comportement normal. Pour une comparaison fiable, repartir d'une session
  neuve sans utiliser O. Cette touche nécessite les commandes clavier/souris.
- **Dessins hors périmètre :** les chemins différés ou exécutés hors de
  `frontSingleInstruments` ne bénéficient pas automatiquement de la correction.
  De plus, `DisableJoy != 0` entraîne le retrait des hooks : le rendu des
  dialogues, cinématiques et transitions reste à vérifier, même pour la police
  décrite comme globale.
- **Échantillonnage du texte :** le facteur nominal de destination `21/16`
  multiplié par le pas source `3/4` donne `63/64`, soit environ 98,44 % de la
  hauteur source. Le calcul réel est `H + 2 * floor(5H/32)` en coordonnées
  10.2 ; les arrondis et la rastérisation influencent donc la couverture exacte.
  Un pas de `3/4` correspond à une destination de `4/3`, tandis qu'une
  destination de `21/16` demanderait un pas de `16/21`. Les commentaires du
  code expliquent le choix actuel par la stabilité du motif d'interpolation.
  La dégradation rapportée concerne ce compromis avec la fonte bitmap native ;
  elle ne démontre pas qu'une autre fonte ou un autre chemin de rendu serait
  incapable de produire une meilleure image.

Ces limites des corrections existantes restent documentées pour la reprise.
Le défaut de choix 48/68 des sprites a été corrigé avec le bandeau ; son
résultat visuel en haute résolution reste à valider.

## Comparaisons à effectuer en jeu

Garder la même scène, la même arme et les mêmes réglages du plugin pour chaque
comparaison. Commencer à 30 fps, en basse résolution, sans diagnostic O.

1. Comparer la référence 4/3, le widescreen natif sans correction du HUD, puis
   le widescreen natif avec correction.
2. Prioriser les deux lignes du compteur, y compris les zéros atténués, les
   passages 100/99/10/9/0 et les changements d'arme. Vérifier que les autres
   éléments du HUD conservent leur rendu validé.
3. Refaire la comparaison en haute résolution pour vérifier l'accord entre les
   cadres, les sprites et le bandeau avec le biais de 68 pixels.
4. Tester les menus, dialogues, cinématiques et retours en partie.
5. Vérifier l'activation/désactivation, un démarrage à froid, un changement de
   niveau et le chargement d'une sauvegarde, puis répéter à 60 fps. Observer
   particulièrement l'ancrage des barres vertes pendant les bascules 4/3 ↔ 16/9.

La compilation et les vérifications d'instructions ne remplacent pas cette
validation du rendu. La trace établit quel renderer dessine le compteur ; elle
ne juge ni ses proportions ni sa lisibilité après correction.
