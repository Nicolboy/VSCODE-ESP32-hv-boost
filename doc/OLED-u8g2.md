# Ecran OLED : migration vers u8g2

L'IHM OLED (`src/oled_ui.cpp`) utilise la librairie u8g2
(`U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI`) depuis la migration du driver
maison (adapte du tutoriel constructeur, retire du depot avant
publication -- voir `doc/references.md`).

## SPI matériel sur broches personnalisees

Le SSD1322 est cable sur des broches qui ne sont pas les broches SPI
par defaut de l'ESP32-C3. `SPI.begin(PIN_OLED_CLK, -1, PIN_OLED_MOSI,
PIN_OLED_CS)` doit etre appele **avant** `u8g2.begin()` pour que le bus
HW SPI d'u8g2 utilise ces broches (voir `OledUi::begin()`).

## Registre remap (0xA0)

Le remap par defaut d'u8g2 pour ce controleur (`0x06` dans
`u8x8_d_ssd1322.c`) ne correspond pas au cablage de notre panneau
(image en miroir horizontal). On reapplique apres `u8g2.begin()` la
valeur validee sur l'ancien driver maison : `u8g2.sendF("caa", 0xA0,
0x14, 0x11)` (COM remap + nibble remap actifs, column remap desactive).

**Ne pas desactiver le nibble remap** (`0x10` au lieu de `0x14`) : deja
teste, ca fait reapparaitre un miroir vertical et rend l'affichage
illisible. u8g2 lui-meme active ce bit dans son propre remap par
defaut (`0x06`), donc il est necessaire quelle que soit la config.

## Logo splash coupe en deux moities (128px)

Le logo de demarrage (256x64, dessine en un seul `drawXBMP`) s'affichait
coupe en deux moities de 128px inversees, alors que le texte du
dashboard (courtes sequences, meme chemin de dessin u8g2 sous-jacent)
s'affichait correctement. Plusieurs hypotheses testees sur le hardware,
**aucune n'a resolu le probleme individuellement** :

1. Bit "nibble remap" du registre 0xA0 -- écarté (voir ci-dessus, a
   empire le probleme).
2. Optimisation RLE de `u8g2_DrawXBMP` (regroupe les runs de pixels en
   longs segments qui traversent la frontiere interne a 128px du
   SSD1322, puce a double controleur de colonnes) -- dessin pixel par
   pixel teste, n'a pas resolu le probleme non plus.
3. `u8g2_uint_t` tronque a 8 bits sur les largeurs >= 256 si
   `U8G2_16BIT` n'est pas actif -- **verifie non applicable** : dans
   u8g2 >= 2.36 (celle utilisee ici, voir `platformio.ini`),
   `U8G2_16BIT` est defini inconditionnellement (fix upstream de
   l'issue olikraus/u8g2#1222), confirme par un `static_assert(sizeof(
   u8g2_uint_t) == 2)` qui compile sans erreur. Le flag `-D U8G2_16BIT`
   est neanmoins ajoute en dur dans `platformio.ini` par precaution,
   au cas ou une future version de la lib restreindrait a nouveau la
   detection auto par architecture (ESP32-C3 = RISC-V, absent de la
   liste `__arm__`/`__xtensa__`/... citee par cette issue).

**Cause racine non identifiee formellement.** Contournement retenu :
dessiner le logo en deux moities de 128px (`lebel_logo_left` /
`lebel_logo_right` dans `src/lebel_logo_xbm.h`) via deux `drawXBMP`
successifs (`OledUi::showSplash()`), aucun appel ne depassant alors la
frontiere des 128px quelle que soit sa cause exacte. Le tableau
`lebel_logo_bits` (image complete 256x64) reste dans le fichier a
titre de reference mais n'est plus utilise et n'est pas garanti complet
(voir taille reelle du tableau si besoin de le reactiver un jour).

**A confirmer sur le hardware** : si le contournement en deux moities
fonctionne, tant mieux. Si le probleme persiste malgre tout, la cause
est ailleurs (registre remap plus subtil, alimentation, cablage) et il
faudra reprendre l'investigation cote registre SSD1322 avec plus de
prudence (deja regresse une fois en touchant ce registre a l'aveugle).
