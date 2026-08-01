#pragma once

#include "protocol.h"

namespace OledUi {

void begin();

// A appeler en boucle. Redessine l'ecran periodiquement et fait tourner
// les pages (entree/commandes, etage 1, etage 2).
void update(const Telemetry &t, bool linkOk);

}  // namespace OledUi
