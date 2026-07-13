// Living Settlements -- core/LifecycleGate.cpp
// ASCII-only. So membros verificados do GameWorld (header sweep).
#include "core/LifecycleGate.h"

#include <kenshi/GameWorld.h>
#include <kenshi/SaveManager.h>
#include <kenshi/SaveFileSystem.h>

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

WriteFence evaluateWriteFence(GameWorld* world) {
    WriteFence f;
    if (world == 0) {
        return f; // mundo ausente -> fechada (defaults: open=false, -1)
    }
    SaveManager* sm = SaveManager::getSingleton();
    SaveFileSystem* sfs = SaveFileSystem::getSingleton();
    f.signal = (sm != 0) ? sm->signal : -1;
    f.saveState = (sfs != 0) ? static_cast<int>(sfs->state) : -1;
    f.loading = world->isLoadingFromASaveGame();
    f.resetting = world->gameResetting;
    f.threadsClear = world->allThreadQueuesAreClear();
    // Fail-closed: singleton ausente (-1), qualquer save/load/reset, ou filas
    // sujas => fechada. Aberta so na calmaria total sem operacao de ciclo-de-vida.
    f.open = (f.signal == 0)
          && (f.saveState == static_cast<int>(SaveFileSystem::NORMAL))
          && !f.loading
          && !f.resetting
          && f.threadsClear;
    return f;
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
