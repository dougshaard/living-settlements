// Living Settlements -- adapters/OrderEmitter.h
// -----------------------------------------------------------------
// UNICO escritor (design secao 4 / 7.3): choke unico, kill-switch e
// auditoria de toda escrita no jogo. GATED OFF ate o Marco 3 (H1/H2
// provados in-game). Fail-closed: so escreve em MODE_OBSERVE_AND_ACT.
//
// Marco 0/1: implementado e compilavel (prova as assinaturas verificadas
// de setDestination/addOrder/reThinkCurrentAIAction), mas NAO e chamado
// pelo POC de sombra -- nenhuma escrita ocorre. O gate composto completo
// (versao, prone, selecao, TP_OBEDIENCE, pre-flight) entra no Marco 3.
//
// Ponteiros DEVEM ser do mesmo tick (nunca cacheados). Verbo por
// constante nomeada (invariante 12).
// -----------------------------------------------------------------
#ifndef LS_ADAPTERS_ORDEREMITTER_H
#define LS_ADAPTERS_ORDEREMITTER_H

#include "core/LifecycleGate.h"

#include <kenshi/Enums.h>   // TaskType (enum -- nao forward-declaravel em C++03)

class Character;
class Building;
class PlayerInterface;
namespace Ogre { class Vector3; }

namespace ls {
namespace adapters {

enum EmitResult {
    EMIT_OK = 0,
    EMIT_BLOCKED_MODE,       // fail-closed: modo != OBSERVE_AND_ACT
    EMIT_BLOCKED_FENCE,      // fail-closed: cerca de escrita fechada (save/load/filas)
    EMIT_BLOCKED_NULL,       // ponteiro do tick invalido
    EMIT_BLOCKED_AUTHORITY   // gate de autoridade composto reprovou
};

// Toda escrita passa por AQUI e so ocorre com writeGateOpen(mode, fence):
// modo OBSERVE_AND_ACT E cerca de save/load ABERTA (P5-2, provada in-game).
// A WriteFence vem de core::evaluateWriteFence, avaliada no MESMO tick.

// Pre-posicionamento (verbo provado por POC-011). CONFIRM = chegada.
EmitResult emitPreposition(core::CoordMode mode, const core::WriteFence& fence,
                           Character* worker, const Ogre::Vector3& pos);

// Operar maquina (addOrder OPERATE_MACHINERY + reThink). CONFIRM =
// worker in currentOperators. [H5] runtime a validar no Marco 3.
EmitResult emitOperate(core::CoordMode mode, const core::WriteFence& fence,
                       Character* worker, Building* station,
                       const Ogre::Vector3& stationPos);

// POC-H11 (P5-3): STAFFAR = addJob permajob DURAVEL (nao addOrder cru). subject =
// a estacao; addDontClear=TRUE (ADITIVO -- nunca limpa os cargos do jogador,
// inv.19). CONFIRM = getPermajobCount sobe e o cargo aponta p/ a estacao [H3/H11].
EmitResult emitAddPermajob(core::CoordMode mode, const core::WriteFence& fence,
                           Character* worker, Building* station, TaskType task,
                           const Ogre::Vector3& stationPos);

// DESSTAFFAR CIRURGICO por slot (removePermajob). O chamador garante MAOS VAZIAS
// (inv.17): nunca arrancar um worker no meio do ciclo de haul.
EmitResult emitRemovePermajob(core::CoordMode mode, const core::WriteFence& fence,
                              Character* worker, int slot);

// H11-v2 METODO 0: staffar via a MESMA funcao da UI de jobs. Age nos personagens
// SELECIONADOS (nao recebe worker). Descoberta (disasm): Character::addJob roteia
// pelo TaskData BASE do task (permaJob=NOT_A_PERMAJOB p/ 87 -> cai em `jobs`
// transiente); addJobSelectedCharacters usa um caminho proprio que monta o
// Tasker-perma. CONFIRM = getPermajobCount do selecionado sobe.
EmitResult emitAddJobSelected(core::CoordMode mode, const core::WriteFence& fence,
                              PlayerInterface* pl, TaskType task, Building* station,
                              const Ogre::Vector3& pos);

// H11-v2 METODO 1 (fallback): "nova tarefa do jogador" -- caminho que a UI usa ao
// CLICAR num alvo. Age nos SELECIONADOS. targetH=hand(station), destinationIndoors
// =station, addDontClear=true.
EmitResult emitNewPlayerTaskSelected(core::CoordMode mode, const core::WriteFence& fence,
                                     PlayerInterface* pl, TaskType task, Building* station,
                                     const Ogre::Vector3& pos);

const char* emitResultName(EmitResult r);

} // namespace adapters
} // namespace ls

#endif // LS_ADAPTERS_ORDEREMITTER_H
