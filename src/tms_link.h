#pragma once

#include "protocol.h"

namespace TmsLink {

void begin();

// A appeler en boucle : lit/parse l'UART (ou simule si TMS_DUMMY_MODE) et
// envoie periodiquement la trame de commande courante.
void poll();

// Met a jour l'etat de commande a transmettre au TMS320 au prochain envoi.
void setCommand(const CommandState &cmd);

const Telemetry &telemetry();
const CommandState &command();

// true si une trame de telemetrie valide (checksum OK) a ete recue
// recemment (ou si on est en mode simulation).
bool linkOk();

// Derniere trame $C envoyee telle quelle (ex. "$C,HT=1,V1SET=35.0,
// VOSET=400.0,RUN=1*XX\n"), pour l'echo "frame_sent" de l'API HTTP
// d'orchestration (voir doc/orchestration.md). Pointeur stable, valide
// jusqu'au prochain appel a setCommand()/poll().
const char *lastFrameSent();

}  // namespace TmsLink
