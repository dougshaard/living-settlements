// Living Settlements -- pocs/Poc030_LimparCargos.cpp
// Limpeza total de permajobs do roster. ASCII-only. So simbolos verificados:
//   Character::getPermajobCount/getPermajob/getPermajobName  Character.h
//   remocao via adapters::emitRemovePermajob (choke unico, cerca de escrita)
// Caps duros: orcamento de remocoes por rodada + guarda por personagem.
#include "pocs/Poc030_LimparCargos.h"
#include "core/PocEnv.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "adapters/OrderEmitter.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/util/lektor.h>

#include <cstdint>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

static const uint32_t WIPE_MAX_CHARS      = 512; // roster do jogador
static const int      WIPE_MAX_PER_CHAR   = 64;  // slots por char (guarda)
static const int      WIPE_BUDGET_ROUND   = 256; // remocoes por rodada (tick nunca
                                                 // desgoverna; roster grande = 2+
                                                 // rodadas, re-tenta sozinho)
int g_totalRemoved = 0;  // acumulado da limpeza em curso (zera ao concluir)
int g_rounds = 0;

} // namespace

void poc030LimparCargosTick(GameWorld* world) {
    const core::PocEnvState& env = core::pocEnv();
    if (!env.clearJobs || world == 0) {
        return;
    }
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return; // load/reset: flag fica armada, tenta na proxima rodada
    }
    PlayerInterface* pl = world->player;
    if (pl == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    if (!core::writeGateOpen(mode, fence)) {
        return; // cerca fechada: idempotente, re-tenta
    }

    ++g_rounds;
    int removed = 0, charsTouched = 0, leftOver = 0;
    bool blocked = false;
    lektor<Character*>& chars = pl->playerCharacters;
    uint32_t n = chars.size();
    if (n > WIPE_MAX_CHARS) {
        n = WIPE_MAX_CHARS;
    }
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c == 0) {
            continue;
        }
        bool touched = false;
        int guard = 0;
        // Remove sempre o slot 0 (a lista encolhe a cada remocao); guarda
        // dupla: por char e pelo orcamento global da rodada.
        while (c->getPermajobCount() > 0 && guard < WIPE_MAX_PER_CHAR
               && removed < WIPE_BUDGET_ROUND && !blocked) {
            if (adapters::emitRemovePermajob(mode, fence, c, 0)
                    != adapters::EMIT_OK) {
                blocked = true; // cerca fechou no meio: continua na proxima
                break;
            }
            ++removed;
            ++guard;
            touched = true;
        }
        if (touched) {
            ++charsTouched;
        }
        leftOver += c->getPermajobCount();
        if (removed >= WIPE_BUDGET_ROUND || blocked) {
            // conta o resto do roster p/ o log e para
            for (uint32_t k = i + 1; k < n; ++k) {
                if (chars[k] != 0) {
                    leftOver += chars[k]->getPermajobCount();
                }
            }
            break;
        }
    }
    g_totalRemoved += removed;

    if (leftOver > 0 || blocked) {
        std::ostringstream s;
        s << "LIMPEZA (rodada " << g_rounds << "): " << removed
          << " cargo(s) removido(s) de " << charsTouched << " char(s); restam "
          << leftOver << " -- continua na proxima rodada.";
        diag::milestone(s.str());
        return;
    }

    std::ostringstream s;
    s << "LIMPEZA CONCLUIDA: " << g_totalRemoved << " cargo(s) removido(s) em "
      << g_rounds << " rodada(s); roster ZERADO. Orquestrador e Guarnicao "
      << "recompoem a cidade nas proximas rodadas (por necessidade e skill). "
      << "Obs: o cargo de medico da POC de observacao nao re-emite nesta "
      << "sessao (single-shot).";
    diag::milestone(s.str());
    g_totalRemoved = 0;
    g_rounds = 0;
    core::pocEnvMutable().clearJobs = false; // desarma: acao unica por clique
}

} // namespace pocs
} // namespace ls
