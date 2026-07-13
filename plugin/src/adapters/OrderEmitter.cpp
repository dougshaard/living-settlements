// Living Settlements -- adapters/OrderEmitter.cpp
// UNICO escritor, GATED OFF. ASCII-only. So assinaturas verificadas.
#include "adapters/OrderEmitter.h"

#include <kenshi/Character.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

namespace ls {
namespace adapters {

namespace {

// Gate de autoridade PARCIAL (o completo entra no Marco 3): nenhum sinal
// sozinho basta (invariante 7.1.3). canTakePlayerOrdersAtThisTime retorna
// TRUE ate para KO [R] -> compor com flags mestras.
bool authorityOk(Character* w) {
    if (w == 0) {
        return false;
    }
    if (w->isDead() || w->isUnconcious()) {
        return false;
    }
    if (!w->canTakePlayerOrdersAtThisTime()) {
        return false;
    }
    return true;
}

} // namespace

EmitResult emitPreposition(core::CoordMode mode, const core::WriteFence& fence,
                           Character* worker, const Ogre::Vector3& pos) {
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return EMIT_BLOCKED_MODE;      // fail-closed
    }
    if (!core::writeGateOpen(mode, fence)) {
        return EMIT_BLOCKED_FENCE;     // fail-closed: save/load/filas em curso
    }
    if (worker == 0) {
        return EMIT_BLOCKED_NULL;
    }
    if (!authorityOk(worker)) {
        return EMIT_BLOCKED_AUTHORITY;
    }
    worker->setDestination(pos, false); // shift=false (nao enfileira)
    return EMIT_OK;
}

EmitResult emitOperate(core::CoordMode mode, const core::WriteFence& fence,
                       Character* worker, Building* station,
                       const Ogre::Vector3& stationPos) {
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return EMIT_BLOCKED_MODE;
    }
    if (!core::writeGateOpen(mode, fence)) {
        return EMIT_BLOCKED_FENCE;     // fail-closed: save/load/filas em curso
    }
    if (worker == 0 || station == 0) {
        return EMIT_BLOCKED_NULL;
    }
    if (!authorityOk(worker)) {
        return EMIT_BLOCKED_AUTHORITY;
    }
    // Verbo por CONSTANTE NOMEADA (nunca ordinal). subject=0 (sem alvo
    // extra); shift=false, clear=false (nao apaga a fila do jogador).
    worker->addOrder(station, OPERATE_MACHINERY, 0, false, false, stationPos);
    worker->reThinkCurrentAIAction();
    return EMIT_OK;
}

EmitResult emitAddPermajob(core::CoordMode mode, const core::WriteFence& fence,
                           Character* worker, RootObject* subject, TaskType task,
                           const Ogre::Vector3& taskPos) {
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return EMIT_BLOCKED_MODE;
    }
    if (!fence.open) {
        return EMIT_BLOCKED_FENCE;      // save/load/filas em curso
    }
    if (worker == 0) {
        return EMIT_BLOCKED_NULL;
    }
    if (!authorityOk(worker)) {
        return EMIT_BLOCKED_AUTHORITY;
    }
    // addJob = permajob DURAVEL, chamada PER-CHAR (mapa-jobs.md verificado no 1.0.65):
    // o router OrdersReceiver::addJob (0x507C40) so cria permajob@0x88 se
    // **shift=TRUE** (Porta 1) E TaskData.permaJob!=0 (Porta 2; 87 e capaz). O run #1
    // falhou por usar shift=FALSE -> caiu em jobs@0x70 (transiente). addDontClear=TRUE
    // = NAO limpa (preserva outros jobs; semantica VERIFICADA, era o inverso do que eu
    // dizia). subject = RootObject* (Character.h:416); NULL e VALIDO (Fase A, MED-13:
    // 0x5C839A test rbx,rbx -> hand nula, identico ao botao Medic nativo) -- por isso
    // NAO ha null-check de subject aqui; o chamador decide se o verbo exige alvo.
    // E metodo de instancia (this=worker), NAO le PlayerInterface -> independe de selecao.
    worker->addJob(task, subject, true, true, taskPos);
    return EMIT_OK;
}

EmitResult emitRemovePermajob(core::CoordMode mode, const core::WriteFence& fence,
                              Character* worker, int slot) {
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return EMIT_BLOCKED_MODE;
    }
    if (!fence.open) {
        return EMIT_BLOCKED_FENCE;
    }
    if (worker == 0 || slot < 0) {
        return EMIT_BLOCKED_NULL;
    }
    // removePermajob por slot (Character.h:418) -- desstaffar cirurgico. O
    // chamador garante maos vazias (inv.17); aqui so o gate de escrita.
    worker->removePermajob(slot);
    return EMIT_OK;
}

EmitResult emitAddJobSelected(core::CoordMode mode, const core::WriteFence& fence,
                              PlayerInterface* pl, TaskType task, Building* station,
                              const Ogre::Vector3& pos) {
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return EMIT_BLOCKED_MODE;
    }
    if (!fence.open) {
        return EMIT_BLOCKED_FENCE;
    }
    if (pl == 0 || station == 0) {
        return EMIT_BLOCKED_NULL;
    }
    // Flags EXATOS capturados do JOBHOOK (o que a UI passa de verdade):
    // shift=TRUE, add=FALSE, pos=(0,0,0). O chamador passa o vetor zero em `pos`.
    pl->addJobSelectedCharacters(task, station, true, false, pos);
    return EMIT_OK;
}

EmitResult emitNewPlayerTaskSelected(core::CoordMode mode, const core::WriteFence& fence,
                                     PlayerInterface* pl, TaskType task, Building* station,
                                     const Ogre::Vector3& pos) {
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return EMIT_BLOCKED_MODE;
    }
    if (!fence.open) {
        return EMIT_BLOCKED_FENCE;
    }
    if (pl == 0 || station == 0) {
        return EMIT_BLOCKED_NULL;
    }
    // Valores EXATOS capturados do JOBHOOK: targetH=hand(station),
    // destinationIndoors=NULL, clickpos=pos (posicao real da estacao),
    // addDontClear=FALSE. hand(RootObjectBase*) -- Building deriva.
    hand targetH(station);
    pl->newPlayerTaskSelectedCharacters(task, targetH, 0, pos, false);
    return EMIT_OK;
}

const char* emitResultName(EmitResult r) {
    switch (r) {
        case EMIT_OK:                 return "OK";
        case EMIT_BLOCKED_MODE:       return "BLOQUEADO_MODO";
        case EMIT_BLOCKED_FENCE:      return "BLOQUEADO_CERCA";
        case EMIT_BLOCKED_NULL:       return "BLOQUEADO_NULO";
        case EMIT_BLOCKED_AUTHORITY:  return "BLOQUEADO_AUTORIDADE";
        default:                      return "?";
    }
}

} // namespace adapters
} // namespace ls
