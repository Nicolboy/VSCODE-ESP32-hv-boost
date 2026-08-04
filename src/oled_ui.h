#pragma once
#include "protocol.h"

namespace OledUi {
void begin();
void update(const Telemetry &t, bool linkOk);
}  // namespace OledUi
