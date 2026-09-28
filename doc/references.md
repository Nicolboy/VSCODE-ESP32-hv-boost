# Références constructeur

Les datasheets et le code d'exemple du fabricant de l'écran OLED ne sont
pas redistribués dans ce dépôt (droits du fabricant). Les récupérer
directement à la source :

## Écran OLED ER-OLEDM032-1 (contrôleur SSD1322)

Fabricant : EastRising Technology Co., Ltd — distribué via
[buydisplay.com](http://www.buydisplay.com).

- Fiche produit et datasheet : chercher « ER-OLEDM032-1 » sur
  buydisplay.com → page produit → onglet « Download » (datasheet PDF).
- Note d'application « Interfacing ER-OLEDM032-1 to Arduino » : même
  page produit, onglet « Download ».
- Datasheet du contrôleur SSD1322 (Solomon Systech) : chercher
  « SSD1322 datasheet » — largement diffusé par les distributeurs de
  composants (Solomon Systech, Newhaven Display, Mouser, etc.), aussi
  fourni en téléchargement sur la page produit buydisplay.com
  ci-dessus.
- Code d'exemple Arduino du fabricant : fourni en téléchargement sur la
  même page produit (« ER-OLEDM032-1_ESP32_Tutorial » et bibliothèque
  associée). Non repris ici : ce dépôt n'utilise plus ce driver, voir
  ci-dessous.

Ce firmware pilote l'écran via la bibliothèque
[U8g2](https://github.com/olikraus/u8g2) (open source, licence
BSD-2-Clause), pas via le code d'exemple du fabricant — voir
`doc/OLED-u8g2.md`. Un ancien driver maison adapté du tutoriel
constructeur a existé (`legacy/er_oled.*`) mais n'est plus utilisé ni
compilé depuis le passage à U8g2 ; il a été retiré du dépôt en même
temps que le code d'exemple constructeur.
