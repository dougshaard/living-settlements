// Living Settlements -- core/LifecycleGate.h
// -----------------------------------------------------------------
// Gate de ciclo de vida (net-new core). Fonte: design secoes 4, 5.2,
// 7.2. Decide o MODO do tick e protege a LEITURA contra load/reset.
//
// Dois sub-gates:
//   (i)  LEITURA (Marco 0): se o jogo esta carregando um save ou
//        resetando, nem enumerar -- uma leitura de objetos meio-
//        construidos e access violation NAO-capturavel (try/catch pega
//        so excecoes C++). Fail-closed => na duvida, SKIP.
//   (ii) ESCRITA (Marco 2): OBSERVE_AND_ACT so quando a escrita foi
//        PROVADA segura in-game (H1/H2). Ate la, OBSERVE_ONLY.
//
// Alem disso, threadReadsSafe(): membros mutados em worker thread
// (currentOperators/productionState) so podem ser lidos quando as filas
// de thread estao limpas [H_THREAD]. Sem isso, adiar essas leituras.
//
// Usa SO simbolos verificados do GameWorld (header sweep):
//   isLoadingFromASaveGame()  GameWorld.h:136 (nao-const)
//   gameResetting             GameWorld.h:243 (membro bool @0x8BA)
//   isPaused()                GameWorld.h:131 (const)
//   allThreadQueuesAreClear() GameWorld.h:93
// -----------------------------------------------------------------
#ifndef LS_CORE_LIFECYCLEGATE_H
#define LS_CORE_LIFECYCLEGATE_H

class GameWorld;

namespace ls {
namespace core {

enum CoordMode {
    MODE_SKIP = 0,        // nem ler (load/reset/mundo ausente)
    MODE_OBSERVE_ONLY,    // ler + calcular em sombra; NAO escrever
    MODE_OBSERVE_AND_ACT  // ler + escrever (so quando writesEnabled e provado)
};

// Fail-closed: mundo ausente / carregando save / resetando => SKIP.
// Senao, OBSERVE_ONLY, ou OBSERVE_AND_ACT se writesEnabled (Marco 2+).
CoordMode evaluateLifecycle(GameWorld* world, bool writesEnabled);

// So ler membros mutados em worker thread quando as filas estao limpas.
bool threadReadsSafe(GameWorld* world);

const char* modeName(CoordMode m);

} // namespace core
} // namespace ls

#endif // LS_CORE_LIFECYCLEGATE_H
