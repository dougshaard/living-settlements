// Living Settlements -- pocs/Poc021_LifecycleProbe.cpp
// Marco 2: edge-log dos sinais de ciclo de vida. ASCII-only.
#include "pocs/Poc021_LifecycleProbe.h"
#include "core/Diagnostics.h"

#include <kenshi/GameWorld.h>

#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Estado da main thread (ADR-014): sem sincronizacao. Ultimos valores
// observados dos 4 sinais -> so logamos quando algum MUDA (edge).
bool g_have = false;
bool g_load = false;
bool g_reset = false;
bool g_paused = false;
bool g_clear = false;
unsigned long g_ticks = 0;

} // namespace

void poc021Probe(GameWorld* world) {
    if (world == 0) {
        return;
    }
    ++g_ticks;

    // Sinais de gate (flags primitivos -- leitura mais segura que existe).
    bool load = world->isLoadingFromASaveGame();
    bool reset = world->gameResetting;
    bool paused = world->isPaused();
    bool clear = world->allThreadQueuesAreClear();

    if (!g_have) {
        std::ostringstream s;
        s << "M2 probe inicial (tick " << g_ticks << "): load=" << load
          << " reset=" << reset << " paused=" << paused
          << " filasLimpas=" << clear;
        diag::milestone(s.str());
        g_have = true;
        g_load = load; g_reset = reset; g_paused = paused; g_clear = clear;
        return;
    }

    if (load != g_load || reset != g_reset || paused != g_paused
        || clear != g_clear) {
        std::ostringstream s;
        s << "M2 EDGE (tick " << g_ticks << "):";
        if (load != g_load)     s << " load " << g_load << "->" << load << ";";
        if (reset != g_reset)   s << " reset " << g_reset << "->" << reset << ";";
        if (paused != g_paused) s << " paused " << g_paused << "->" << paused << ";";
        if (clear != g_clear)   s << " filasLimpas " << g_clear << "->" << clear << ";";
        diag::milestone(s.str());
        g_load = load; g_reset = reset; g_paused = paused; g_clear = clear;
    }
}

} // namespace pocs
} // namespace ls
