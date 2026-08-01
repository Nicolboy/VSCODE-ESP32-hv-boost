#pragma once

// Stub pour l'instant : la priorite est de valider l'affichage OLED avec
// la simulation TMS320 avant de brancher le serveur web (qui devra se
// connecter au reseau local, en mode STA, avec des identifiants a fournir).

namespace WebUi {

void begin();
void poll();

}  // namespace WebUi
