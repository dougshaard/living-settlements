// Living Settlements -- pocs/Poc031_Medicos.cpp
// Papel MEDICO reconciliado. ASCII-only. So simbolos verificados:
//   Character::addJob/getPermajob*/removePermajob      Character.h (H3/H11)
//   MedicalSystem::needsFirstAidScoreTotal_*           MedicalSystem.h:223-224
//   Inventory::hasItemFunction/ITEM_FIRSTAID           Inventory.h:195/Enums.h:227
//   CharStats::getStat(STAT_MEDIC)                     Enums.h:657 (padrao Poc028)
//   Tasker::subject (hand) -> getBuilding              Tasker.h/hand.h (padrao F8)
// Escrita APENAS via OrderEmitter atras de writeGateOpen. Caps duros.
#include "pocs/Poc031_Medicos.h"
#include "core/PocEnv.h"
#include "core/Porters.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "adapters/OrderEmitter.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Tasker.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <string>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

static const uint32_t MEDR_MAX_CHARS      = 512;
static const int      MEDR_MAX_SLOTS      = 64;
static const int      MEDR_MAX_CAND       = 256;
static const int      MEDR_TARGET_CAP     = 3;   // teto de medicos simultaneos
static const int      MEDR_WOUNDED_PER    = 25;  // +1 medico a cada N feridos
static const int      MEDR_MAX_EMIT_PER   = 2;   // novos medicos por rodada
static const int      MEDR_MAX_REBUILDS   = 1;   // reconstrucoes por rodada

unsigned long g_round = 0;
unsigned long g_lastLog = 0;

bool eligible(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

// Cargo re-adicionavel numa reconstrucao? So os verbos de TRABALHO conhecidos
// (constantes nomeadas, inv.12). O 234 (cargo de torre criado pelo router) e
// re-emitido como USE_TURRET -- o swap 146->234 e o caminho PROVADO (TUR-1).
bool reAddableTask(TaskType k, TaskType& emitAs) {
    switch (k) {
        case MAN_A_TURRET_PLAYER_JOB:
            emitAs = USE_TURRET;
            return true;
        case OPERATE_MACHINERY:
        case OPERATE_AUTOMATIC_MACHINERY:
        case OPERATE_STORAGE:
        case FILL_MACHINE:
        case COLLECT_OUTPUT_RESOURCE:
        case EMPTY_MACHINE_OUTPUTS:
        case BUILD:
        case USE_TURRET:
            emitAs = k;
            return true;
        default:
            return false;
    }
}

bool isTurretDuty(TaskType k) {
    return k == USE_TURRET || k == MAN_A_TURRET_PLAYER_JOB;
}

bool hasMedicJob(Character* c) {
    int n = c->getPermajobCount();
    if (n > MEDR_MAX_SLOTS) {
        n = MEDR_MAX_SLOTS;
    }
    for (int s = 0; s < n; ++s) {
        if (c->getPermajob(s) == JOB_MEDIC) {
            return true;
        }
    }
    return false;
}

// Perfil de candidatura: livre? guarda? todos os cargos reconstruiveis?
void profileChar(Character* c, bool& isFree, bool& guard, bool& rebuildable) {
    int n = c->getPermajobCount();
    if (n > MEDR_MAX_SLOTS) {
        n = MEDR_MAX_SLOTS;
    }
    isFree = (n == 0);
    guard = false;
    rebuildable = true;
    for (int s = 0; s < n; ++s) {
        TaskType k = c->getPermajob(s);
        if (isTurretDuty(k)) {
            guard = true; // guarda guarda (diretriz 10): fora da candidatura
        }
        TaskType dummy;
        if (!reAddableTask(k, dummy)) {
            rebuildable = false; // cargo desconhecido: nao mexer neste char
        }
    }
}

bool underDirectOrder(Character* c) {
    CharBody* body = c->getBody();
    if (body == 0) {
        return false;
    }
    Tasker* action = body->getCurrentAction();
    return action != 0
        && static_cast<int>(action->priority) >= static_cast<int>(TP_OBEDIENCE);
}

bool selectedByPlayer(PlayerInterface* pl, Character* c) {
    hand sel = pl->selectedCharacter;
    return sel.isValid() && sel.getCharacter() == c;
}

// Reconstrucao da lista de cargos com MEDICO no topo (mapa sec.8.5: ordem =
// prioridade; addJob apenda no fim -> re-emitir na ordem certa). Captura
// (verbo, subject, pos) de ate MEDR_MAX_SLOTS cargos ANTES de remover.
// Retorna false se qualquer emissao for bloqueada (proxima rodada re-tenta;
// idempotente: o char ou ficou como estava ou ja e medico).
bool rebuildWithMedicFirst(core::CoordMode mode, const core::WriteFence& fence,
                           Character* c) {
    struct Captured {
        TaskType  emitAs;
        Building* subj;      // tick-scoped (usado nesta mesma funcao)
        Ogre::Vector3 pos;
    };
    Captured caps[MEDR_MAX_SLOTS];
    int ncap = 0;
    int n = c->getPermajobCount();
    if (n > MEDR_MAX_SLOTS) {
        n = MEDR_MAX_SLOTS;
    }
    for (int s = 0; s < n && ncap < MEDR_MAX_SLOTS; ++s) {
        TaskType k = c->getPermajob(s);
        TaskType emitAs;
        if (!reAddableTask(k, emitAs)) {
            return false; // guarda dupla (profileChar ja filtrou)
        }
        Building* subj = 0;
        const Tasker* tk = c->getPermajobData(s);
        if (tk != 0) {
            hand h = tk->subject;
            if (h.isValid()) {
                subj = h.getBuilding();
            }
        }
        caps[ncap].emitAs = emitAs;
        caps[ncap].subj = subj;
        caps[ncap].pos = (subj != 0) ? subj->getPosition() : c->getPosition();
        ++ncap;
    }
    // Remove tudo (slot 0 repetido; a lista encolhe).
    for (int guard = 0; guard < MEDR_MAX_SLOTS && c->getPermajobCount() > 0;
         ++guard) {
        if (adapters::emitRemovePermajob(mode, fence, c, 0)
                != adapters::EMIT_OK) {
            return false;
        }
    }
    // MEDICO primeiro (topo = prioridade maxima), depois os antigos na ordem.
    Ogre::Vector3 cp = c->getPosition();
    if (adapters::emitAddPermajob(mode, fence, c, 0, JOB_MEDIC, cp)
            != adapters::EMIT_OK) {
        return false;
    }
    for (int i = 0; i < ncap; ++i) {
        adapters::emitAddPermajob(mode, fence, c, caps[i].subj,
                                  caps[i].emitAs, caps[i].pos);
    }
    return true;
}

} // namespace

void poc031MedicosTick(GameWorld* world) {
    if (!core::pocEnv().medicRole || world == 0) {
        return;
    }
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return;
    }
    PlayerInterface* pl = world->player;
    if (pl == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    ++g_round;
    bool throttle = (g_round - g_lastLog) >= 6;
    // Feridos (floats de fase THREADED) e kits (inventario) exigem filas
    // limpas (inv.21); a rodada toda e adiavel sem perda (idempotente).
    if (!core::writeGateOpen(mode, fence) || !fence.threadsClear) {
        return;
    }

    lektor<Character*>& chars = pl->playerCharacters;
    uint32_t n = chars.size();
    if (n > MEDR_MAX_CHARS) {
        n = MEDR_MAX_CHARS;
    }

    // ---- 1) Censo: feridos, medicos atuais, candidatos ----
    int wounded = 0, medics = 0, medicsWithKit = 0;
    Character* cand[MEDR_MAX_CAND];
    bool candKit[MEDR_MAX_CAND];
    bool candFree[MEDR_MAX_CAND];
    int  candSkill[MEDR_MAX_CAND];
    int ncand = 0;
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c == 0) {
            continue;
        }
        MedicalSystem* med = c->getMedical();
        if (med != 0 && (med->needsFirstAidScoreTotal_fleshy > 0.0f
                         || med->needsFirstAidScoreTotal_robot > 0.0f)) {
            ++wounded;
        }
        if (!eligible(c)) {
            continue;
        }
        Inventory* inv = c->getInventory();
        bool kit = (inv != 0 && inv->hasItemFunction(ITEM_FIRSTAID));
        if (hasMedicJob(c)) {
            ++medics;
            if (kit) {
                ++medicsWithKit;
            }
            continue; // ja e medico: nao e candidato
        }
        if (selectedByPlayer(pl, c) || underDirectOrder(c)) {
            continue; // autoridade do jogador e sagrada (inv.7.1.3)
        }
        if (core::isPorter(c)) {
            continue; // carregador declarado: pensamento isolado (17/07)
        }
        bool isFree = false, guard = false, rebuildable = false;
        profileChar(c, isFree, guard, rebuildable);
        if (guard || (!isFree && !rebuildable)) {
            continue; // guarda guarda; cargo desconhecido nao se mexe
        }
        if (ncand < MEDR_MAX_CAND) {
            cand[ncand] = c;
            candKit[ncand] = kit;
            candFree[ncand] = isFree;
            CharStats* st = c->getStats();
            candSkill[ncand] = (st != 0)
                ? static_cast<int>(st->getStat(STAT_MEDIC, false)) : -1;
            ++ncand;
        }
    }

    int target = 1 + wounded / MEDR_WOUNDED_PER;
    if (target > MEDR_TARGET_CAP) {
        target = MEDR_TARGET_CAP;
    }

    // ---- 2) Deficit? Reconciliar (repoe o que morte/limpeza desfez) ----
    int deficit = target - medics;
    if (deficit <= 0) {
        if (throttle && wounded > 0 && medics > medicsWithKit) {
            std::ostringstream s;
            s << "MEDICOS: " << medics << "/" << target << " de plantao mas so "
              << medicsWithKit << " com kit VALIDO -- sem kit o cargo nao "
              << "executa (I-25); faltam kits (janela de demandas).";
            diag::milestone(s.str());
            g_lastLog = g_round;
        }
        return;
    }

    int emitted = 0, rebuilds = 0;
    while (deficit > 0 && emitted < MEDR_MAX_EMIT_PER) {
        // Melhor candidato: kit > livre > skill (kit decide se cura DE FATO).
        int best = -1;
        for (int i = 0; i < ncand; ++i) {
            if (cand[i] == 0) {
                continue;
            }
            if (best < 0) {
                best = i;
                continue;
            }
            if (candKit[i] != candKit[best]) {
                if (candKit[i]) best = i;
                continue;
            }
            if (candFree[i] != candFree[best]) {
                if (candFree[i]) best = i;
                continue;
            }
            if (candSkill[i] > candSkill[best]) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        Character* c = cand[best];
        cand[best] = 0; // consome o candidato nesta rodada
        bool ok;
        if (candFree[best]) {
            // Livre: cargo direto (lista vazia -> MEDICO vira o topo natural).
            Ogre::Vector3 cp = c->getPosition();
            ok = (adapters::emitAddPermajob(mode, fence, c, 0, JOB_MEDIC, cp)
                  == adapters::EMIT_OK);
        } else {
            if (rebuilds >= MEDR_MAX_REBUILDS) {
                continue; // proxima rodada reconstroi outro (suave)
            }
            ok = rebuildWithMedicFirst(mode, fence, c);
            ++rebuilds;
        }
        if (ok) {
            ++emitted;
            --deficit;
            std::ostringstream s;
            s << "MEDICOS: \"" << c->getName() << "\" escalado ("
              << (candFree[best] ? "livre" : "RECONSTRUIDO com Curar no topo")
              << ", kit=" << (candKit[best] ? "sim" : "NAO")
              << ", skill=" << candSkill[best] << ") -- " << (medics + emitted)
              << "/" << target << " medicos; feridos=" << wounded;
            diag::milestone(s.str());
        }
    }

    if (deficit > 0 && emitted == 0 && throttle) {
        std::ostringstream s;
        s << "MEDICOS: deficit de " << deficit << " (alvo " << target
          << ", feridos=" << wounded << ") e NENHUM candidato apto "
          << "(sem kit/livre/reconstruivel) -- janela de demandas: faltam "
          << "kits ou gente elegivel.";
        diag::milestone(s.str());
        g_lastLog = g_round;
    }
}

} // namespace pocs
} // namespace ls
