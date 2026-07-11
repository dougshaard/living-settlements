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

class Character;
class Building;
namespace Ogre { class Vector3; }

namespace ls {
namespace adapters {

enum EmitResult {
    EMIT_OK = 0,
    EMIT_BLOCKED_MODE,       // fail-closed: modo != OBSERVE_AND_ACT
    EMIT_BLOCKED_NULL,       // ponteiro do tick invalido
    EMIT_BLOCKED_AUTHORITY   // gate de autoridade composto reprovou
};

// Pre-posicionamento (verbo provado por POC-011). CONFIRM = chegada.
EmitResult emitPreposition(core::CoordMode mode, Character* worker,
                           const Ogre::Vector3& pos);

// Operar maquina (addOrder OPERATE_MACHINERY + reThink). CONFIRM =
// worker in currentOperators. [H5] runtime a validar no Marco 3.
EmitResult emitOperate(core::CoordMode mode, Character* worker,
                       Building* station, const Ogre::Vector3& stationPos);

const char* emitResultName(EmitResult r);

} // namespace adapters
} // namespace ls

#endif // LS_ADAPTERS_ORDEREMITTER_H
