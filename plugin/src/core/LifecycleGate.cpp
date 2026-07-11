// Living Settlements -- core/LifecycleGate.cpp
// ASCII-only. So membros verificados do GameWorld (header sweep).
#include "core/LifecycleGate.h"

#include <kenshi/GameWorld.h>

namespace ls {
namespace core {

CoordMode evaluateLifecycle(GameWorld* world, bool writesEnabled) {
    // Fail-closed: qualquer sinal de load/reset (ou mundo ausente) => SKIP.
    // Nem enumerar: leitura durante o load e AV nao-capturavel (design 5.2).
    if (world == 0) {
        return MODE_SKIP;
    }
    if (world->isLoadingFromASaveGame()) {
        return MODE_SKIP;
    }
    if (world->gameResetting) {          // [H2] semantica; tratado como reset
        return MODE_SKIP;
    }

    // Seguro para LER. Escrever so quando provado (H1/H2) e habilitado.
    if (!writesEnabled) {
        return MODE_OBSERVE_ONLY;
    }
    return MODE_OBSERVE_AND_ACT;
}

bool threadReadsSafe(GameWorld* world) {
    // currentOperators (std::set<hand>) e productionState sao mutados em
    // threadedUpdate (worker thread). Le-los sem quiescencia = UB. [H_THREAD]
    return world != 0 && world->allThreadQueuesAreClear();
}

const char* modeName(CoordMode m) {
    switch (m) {
        case MODE_SKIP:            return "SKIP";
        case MODE_OBSERVE_ONLY:    return "OBSERVE_ONLY";
        case MODE_OBSERVE_AND_ACT: return "OBSERVE_AND_ACT";
        default:                   return "?";
    }
}

} // namespace core
} // namespace ls
