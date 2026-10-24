# Chènevières Chorale — notes de développement

Récapitulatif pour reprendre le travail à tout moment (Studio des Chènevières, Mathieu Chocat).
Projet démarré le 24/10/2026 ; même chaîne que Chènevières EQ (JUCE 8.0.9, CMake, GitHub Actions, Inno Setup, pkgbuild).

## Fichiers

| Fichier | Rôle |
|---|---|
| `Source/ChoirEngine.*` | Tout le traitement audio, **sans JUCE** (testable seul). |
| `Source/PluginProcessor.*` | Paramètres (APVTS), MIDI, état, préréglages. |
| `Source/PluginEditor.*` | Interface (taille de référence 1240 × 800, mise à l'échelle). |
| `Source/StageView.*` | Scène vue de dessus (déplacement des pupitres). |
| `Source/Theme.h` | Couleurs, polices (Cinzel du studio), dessin des contrôles. |
| `Source/ParamIDs.h` | Identifiants et libellés des paramètres. |
| `Source/Presets.*` | Préréglages d'usine (le n° 0 = état par défaut, vérifié par le test). |
| `tests/EngineTest.cpp` | Tests du moteur ; écrit aussi des WAV d'écoute ; traite un fichier WAV. |
| `tests/ProcessorTest.cpp`, `tests/Snapshot.cpp` | Test du processeur ; capture `docs/apercu.png`. |

## Comment ça marche (moteur)

1. **Entrée** : mono (moyenne L/R). Un retard de `latence` échantillons garde la voix principale alignée sur le chœur.
2. **Détection de hauteur** : YIN sur le signal décimé à ~12 kHz (anticrénelage Butterworth d'ordre 4), pas de 2,7 ms,
   75–1100 Hz, seuil 0,13, médiane sur 3 estimations, hystérésis de voisement (0,2 / 0,32), porte à -49 dBFS.
   Contre l'erreur d'octave vers l'aigu (voyelles ouvertes, 2e harmonique fort : vu 25 ms d'erreur sur la voix de Mathieu) :
   le double / triple de la période est préféré s'il prolonge la note en cours (au plus 16 estimations ≈ 43 ms) ou s'il est
   très net alors que le premier creux est médiocre. **Ne pas** préférer le multiple dès qu'il est « un peu meilleur » :
   sur une voix bien périodique c'est souvent le cas, et une erreur d'octave vers le grave fait chanter tout le chœur une octave trop bas.
3. **Marques de période** (PSOLA) : une par cycle. Attaque de note : sommet le plus haut du signal filtré (passe-bas
   1,1 kHz, 2 pôles). Pendant la note : **corrélation normalisée** avec la période précédente (±18 % de période,
   interpolation parabolique → sous-échantillon), ancrée lentement (8 %) sur le sommet pour ne pas dériver.
   Le sommet seul faisait varier l'écart entre marques de 256 à 269 échantillons sur une note tenue : chaque erreur
   devenait une irrégularité de période chez le choriste (jitter ×5, son rugueux). Mesure : `pip install praat-parselmouth`,
   « Get jitter (local) » sur un choriste seul ; hors voisement, marques régulières toutes les 6 ms.
   Chaque marque porte l'intervalle de chaque pupitre (calculé une fois pour tous les choristes).
4. **Harmonie** (`sectionOffset`) :
   - diatonique : note de gamme la plus proche (hystérésis de 0,75 demi-ton), degré + pas de l'intervalle ;
     l'écart est appliqué à la hauteur réellement chantée (on garde la justesse et le vibrato du chanteur) ;
   - intervalles fixes : demi-tons de `kIntervals` ;
   - MIDI : S prend la note la plus haute de l'accord, B la plus basse ; octave la plus proche du centre de la tessiture ;
     l'accord reste « verrouillé » quand on relâche les touches ;
   - tessiture automatique : **une octave par pupitre**, choisie d'après la hauteur moyenne (~2,5 s) — le plus petit
     déplacement qui tombe à 3 demi-tons à l'intérieur de `kRegisters` — et revue seulement en début de note
     (sinon la ligne saute d'octave en pleine mélodie) ; filet de sécurité à ±5 demi-tons hors tessiture.
   - la note de référence est le **demi-ton chanté** (hystérésis 0,75), même en diatonique : l'intervalle vient de la
     gamme mais s'applique à ce demi-ton (une note hors gamme reste juste à l'unisson et à l'octave).
   - chaque marque porte la part de l'écart du chanteur que le choriste **ne suit pas** (`dev`) : il suit la justesse
     lente (moyenne ~80 ms : glissés, notes basses/hautes) à 90 % et les variations rapides (vibrato) à 20 %.
     Tout suivre = parallèle robotique ; ne rien suivre (essai à 30 %) = chœur faux par rapport à la voix sur les glissés.
   - non voisé à moins de 50 ms d'une note : le grain est transposé de l'intervalle de cette note (attaques).
5. **Choristes réels** (`spawnGrain`) : PSOLA, un grain de Hann par période ; vers le grave, grain **asymétrique**
   (0,3 période avant la marque à l'octave au-dessous, 1 période après) : sinon la fin de la période précédente garde
   la note d'origine audible et l'octave inférieure reste 18 dB trop faible (mesuré sur une vraie voix) ;
   formants = grain lu plus vite / plus lentement (facteur `fF`) ; gain √(fF / r).
   Chaque choriste a ses traits tirés une fois (graine fixe : le même chœur à chaque ouverture) :
   désaccord fixe, dérive lente (nouvelle cible toutes les 0,35–1,45 s), vibrato 4,7–6,2 Hz qui s'installe après 150 ms,
   glissé 18–88 ms, retard d'attaque, ±3,5 % de formants, ±1,6 dB.
   Le retard d'un choriste varie au plus de 0,0025 échantillon par échantillon (≈ 4 cents) : pas de saut quand on règle.
6. **Scène** (`updateGeometry`) : largeur 4–42 m selon la taille, profondeur 0,55 × largeur, auditeur devant à `distance`.
   Pour chaque choriste : panoramique (sin de l'azimut), gain 1/d, passe-bas de l'air (19 kHz / (1 + d/20)),
   retard de propagation (d − distance) / 343 m/s.
   Répartition exacte des choristes entre pupitres (au moins un par pupitre allumé).
7. **Masse** : au-delà de 8/16/32 choristes calculés par pupitre, 8 copies du pupitre (6–38 ms, modulées 0,15–0,5 ms
   à 0,05–0,3 Hz), placées au hasard sur la surface du pupitre. Puissance : réels 1/E, masse (E−1)/E, E = représentés / calculés.
   Un grand chœur sonne un peu plus fort : +16 % × log10(N/3).
8. **Souffle** : bruit filtré (passe-bande ~4 kHz) qui suit l'enveloppe de la voix retardée.
9. **Acoustique** (`Hall`) : réseau de 16 retards + Hadamard, amortissement par ligne (temps de réverbération grave/aigu),
   4 passe-tout de diffusion par canal, 12 premières réflexions, 2 lignes modulées.
   Lieux : Studio 0,55 s, Chapelle 1,7 s, Église 3,4 s, Cathédrale 6,8 s. Changer de lieu : fondu de sortie → nouvelle salle → fondu d'entrée.

**Latence** = 4 périodes de la note la plus grave (75 Hz) + 2 pas d'analyse ≈ 59 ms à 48 kHz, annoncée au DAW.
Ne pas la raccourcir sans revoir `processVoice` : un choriste a besoin des marques jusqu'à 1,5 période après sa position,
et les marques ne sont connues qu'à 2 périodes + 1 pas du présent.

## Mesures (Linux, 1 cœur, 48 kHz, tests du 24/10/2026)

- Détection : erreur moyenne 0,17 demi-ton (voix de synthèse avec vibrato et glissés), aucun saut d'octave.
- Intervalles produits : à ±0,02 demi-ton (unisson, tierces, quinte, octaves).
- CPU : ~6 % (3–10 choristes), ~15 % en Éco/Normale (≥ 50), ~43 % en Haute (128 choristes calculés).
- Niveau stable de 3 à 1000 choristes (-15 à -17 dB RMS pour une voix à -6 dBFS crête).

## Tests (sous Linux)

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/chemin/JUCE -DCHNV_TESTS=ON
cmake --build build
build/EngineTest                       # doit finir par « TOUT EST OK »
build/EngineTest wav dossier           # + fichiers d'écoute (voix de synthèse, 10 / 50 / 100 / 1000 choristes)
build/EngineTest voix.wav sortie.wav [taille 0-8] [lieu 0-3] [femme 0/1] [tonique 0-11] [mode 0-4]
Xvfb :98 -screen 0 2560x1600x24 +extension RANDR &   # puis DISPLAY=:98
build/ProcessorTest                    # dispositions, préréglages, sauvegarde, MIDI
build/ChoraleSnapshot docs/apercu.png [preset=N] [size=N] [scale=1.5]
```

## Comment on publie une version

1. La version est dans `CMakeLists.txt` (`project(ChenevieresChorale VERSION x.y.z)`).
2. Chaque envoi de code lance le build (Mac + Windows) ; les installeurs sont dans les Artifacts du run.
3. Signature Apple : mêmes secrets que l'EQ, à ajouter dans les secrets **de ce dépôt** (`MACOS_CERT_APP_P12`,
   `MACOS_CERT_INSTALLER_P12`, `MACOS_CERT_PASSWORD`, `NOTARY_API_KEY`, `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID`).
   Sans eux : signature ad hoc (`sudo xattr -cr` après installation).
4. AAX : construit (SDK fourni avec JUCE 8) ; signature PACE à faire comme pour l'EQ.

## Historique des versions

| Version | Contenu |
|---|---|
| 1.2.0 | Timbre du chœur réglé sur Wakhama Voice Choir **avec la même voix** (voix sèche de la vidéo passée dans les deux plugins) : coupe-bas 130 Hz (Q 0,6), présence (étagère 700 Hz +8,5 dB), air (étagère 7 kHz = Brillance) ; écart par bande d'octave 60 Hz–16 kHz ramené de 3,2 à ~1,2 dB en moyenne. Le chœur était terne (centroïde 615 Hz contre 966) et trop chargé dans le grave (+25 dB contre +15). Afro / Gospel : brillance 7, désaccord 14, souffle 30. `/tmp/claude-0/ref/bands.py` compare les bandes. |
| 1.1.1 | Retours de Mathieu (« à 12 s le pitch n'est pas terrible, à partir de 17 s les octaves ne marchent pas ») : (1) la note de référence est le **demi-ton chanté** et non la note de gamme voisine — un ré naturel en mi majeur faisait osciller unisson et octave entre do♯ et ré♯ ; l'intervalle reste tiré de la gamme ; (2) les choristes suivent la justesse **lente** du chanteur (glissés, attaques par en dessous : 90 %, filtre ~80 ms) mais pas ses variations rapides (vibrato : 13 % mesuré) ; (3) retards resserrés : pupitres 6–13 ms (au lieu de 18–34), attaques 25 ms par défaut, 3–7 ms et 8 ms pour Afro / Gospel — sur une mélodie qui saute d'octave, 40–60 ms de retard faisait entendre l'ancienne note ; (4) détecteur : la note est gardée jusqu'au bout de sa queue (seuils 0,45 / -56 dBFS), fins de note transposées jusqu'à 150 ms. Mesure (pupitre seul vs voix, même instant, ±35 cents) : tierce 89,6 %, unisson 86 %, octave 83 %. |
| 1.1.0 | Stéréo : panoramique d'après la place latérale sur la scène (+ écart interaural ≤ 0,62 ms) au lieu de l'angle vu de loin — le chœur était presque mono (corrélation G/D +0,9). Nouveau réglage **Brillance** (étagère 6,5 kHz, -6…+9 dB, +3 par défaut), disposition par défaut plus large, préréglage **Afro / Gospel studio** calqué sur une vidéo de référence (Wakhama Voice Choir, « Afrobeat Choir ») envoyée par Mathieu : même signature stéréo (chœur seul : corrélation -0,16, référence -0,17). Les préréglages peuvent fixer la place des pupitres. |
| 1.0.3 | Marques de période par corrélation avec la période précédente, au sous-échantillon près (+ ancrage lent sur le sommet) : jitter d'un choriste 2,1 % → 0,55 % (voix d'origine 0,43 %, mesuré avec Praat) — c'était la principale cause du son « numérique ». Consonnes plus discrètes chez les choristes (grains non voisés à 0,4), vibrato dont la vitesse et l'amplitude flottent. |
| 1.0.2 | Moins « robotique » (retour de Mathieu) : les choristes ne recopient plus la justesse fine du chanteur (ils chantent la note juste et ne suivent que 30 % de ses inflexions : vibrato, glissés), timbre plus varié d'un choriste à l'autre (formants ±6,5 %, couleur des aigus, niveau qui respire), attaques redistribuées à chaque phrase, masse sans effet de flanger (retards 16–71 ms, double modulation lente, passe-tout de décorrélation). |
| 1.0.1 | Après écoute des voix de Mathieu (24/10/2026) : basse vraiment à l'octave (grain asymétrique vers le grave, sous-octave passée de -18 dB à -3 dB sous la fondamentale), plus d'erreur d'octave du détecteur sur les voyelles à 2e harmonique fort (continuité ≤ 40 ms), tessiture automatique par phrase (une octave par pupitre, revue seulement en début de note), attaques transposées dès la première consonne voisée. |
| 1.0.0 | Première version : 9 tailles (3–1000), 4 pupitres SATB, harmonie diatonique / intervalles / MIDI, formants SATB, tessiture automatique, scène vue de dessus, réalisme humain, 4 lieux, 12 préréglages. Interface en français. |

## Mesures sur les voix de Mathieu (24/10/2026, deux voix d'homme sèches, 48 kHz)

Chaque pupitre seul, hauteur comparée trame par trame (pyin) sur les passages audibles :
- voix B (notes tenues, mi majeur) : soprano à l'octave 98 %, basse à l'octave 97 %, alto et ténor à la bonne tierce / sixte
  (majeure ou mineure selon la note) ~99 % ;
- voix A (très ornée) : 86–89 % ; le reste = ornements mesurés en passage.
Fichiers de test : `tests/` ne les contient pas (voix de Mathieu, à garder privées).

## Comparaison avec une référence (24/10/2026)

Vidéo de Wakhama Voice Choir (8 voix, Room 30 %, largeur 70 %, mix 60 %) : `/tmp/claude-0/ref/measure.py` calcule la corrélation
G/D, le rapport côtés/centre, l'énergie > 4 et > 8 kHz, le centroïde et l'« étalement » des harmoniques (platitude 150–1500 Hz).
La référence : corrélation -0,17, côtés +1,5 dB au-dessus du centre, harmoniques très étalées, brillance ×7 par rapport à la voix seule.

## En attente / idées

- Écoute sur de vraies voix sèches de Mathieu (homme et femme) et réglage fin des valeurs par défaut.
- Langues de l'interface (l'EQ en a 25 : reprendre `Lang` et `tools/translations*.py`).
- Mises à jour (`Updates.h` de l'EQ + dépôt public `chenevieres-chorale-updates`), manuel PDF, compte Chènevières.
- Voyelles / textes chantés différents par pupitre ? Sibilantes adoucies sur les grands chœurs ?
