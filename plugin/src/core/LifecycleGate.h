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

// -----------------------------------------------------------------
// Cerca de ESCRITA (parte 5 secao 6). Fail-closed: so seria seguro escrever
// quando NENHUM sinal de save/load/reset/import/newgame esta ativo E as filas
// estao limpas. Fundamentada na RUN 2026-07-11 (POC-SAVE-1): SaveManager::signal
// lidera ~2 ticks; SaveFileSystem::state cobre a serializacao -> a composicao e
// hermetica. Em SOMBRA e apenas COMPUTADA e logada -- NAO gateia nada ainda.
// -----------------------------------------------------------------
struct WriteFence {
    bool open;          // true = seria seguro escrever agora
    int  signal;        // SaveManager::signal (0=nada; -1=singleton ausente)
    int  saveState;     // SaveFileSystem::state (0=NORMAL; -1=ausente)
    bool loading;       // isLoadingFromASaveGame
    bool resetting;     // gameResetting
    bool threadsClear;  // allThreadQueuesAreClear
    WriteFence() : open(false), signal(-1), saveState(-1),
                   loading(false), resetting(false), threadsClear(false) {}
};

WriteFence evaluateWriteFence(GameWorld* world);

// Predicado PURO (sem GameWorld) que combina os DOIS gates de escrita: o modo
// do tick (OBSERVE_AND_ACT, ja exige writesEnabled + sem load/reset) E a cerca
// hermetica de save/load (WriteFence). Fail-closed: qualquer um fechado => sem
// escrita. Inline de proposito -- e o unico ponto de decisao de escrita e fica
// testavel no harness puro (sem KenshiLib). O OrderEmitter (choke unico) o usa.
inline bool writeGateOpen(CoordMode mode, const WriteFence& fence) {
    return mode == MODE_OBSERVE_AND_ACT && fence.open;
}

} // namespace core
} // namespace ls

#endif // LS_CORE_LIFECYCLEGATE_H
