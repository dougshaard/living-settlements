// Living Settlements -- pocs/Poc028_Guarnicao.cpp
// GUARNICAO (1a fatia do reconciliador). ASCII-only. So simbolos verificados:
//   Building::getBuildingClass()/BCTYPE_TURRET      Building.h:249/40
//   UseableStuff::numOperatorsMax/isFreeSlot/getStatUsed/getDefaultTask
//   Character::addJob/getPermajob*/getStats          Character.h
//   CharStats::getStat                                (padrao Poc025)
// Escrita APENAS via OrderEmitter atras de writeGateOpen. Caps duros em todo
// laco (guardrail do hang). Idempotente: dedup nativo (1 cargo por char+torre)
// + checagem por chave ANTES de emitir; re-varre a cada rodada e recompoe
// torre desguarnecida (morte/KO/remocao) sozinho -- o REBALANCEAR que o jogo
// nao tem.
#include "pocs/Poc028_Guarnicao.h"
#include "core/PocEnv.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "core/LsConfig.h"
#include "adapters/OrderEmitter.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Tasker.h>
#include <kenshi/Town.h>
#include <kenshi/InstanceID.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <cstdlib>   // free()
#include <algorithm> // std::sort
#include <string>
#include <vector>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Caps duros.
static const uint32_t GAR_MAX_CHARS     = 512;
static const int      GAR_MAX_SLOTS     = 64;
static const int      GAR_MAX_TURRETS   = 64;
static const int      GAR_MAX_EMIT_PER  = 4;   // emissoes por rodada (suave)
static const int      GAR_MAX_CAND      = 256;

unsigned long g_round = 0;
unsigned long g_lastLog = 0;

// Cargos que marcam um char como NAO-candidato a guarda: producao/medico/
// logistica. Treino e torre sao PARTE do pacote do guarda (diretriz 10).
// Constantes NOMEADAS (inv.12).
bool isNonGuardDuty(TaskType k) {
    switch (k) {
        case OPERATE_MACHINERY:
        case OPERATE_AUTOMATIC_MACHINERY:
        case OPERATE_STORAGE:
        case FILL_MACHINE:
        case COLLECT_OUTPUT_RESOURCE:
        case EMPTY_MACHINE_OUTPUTS:
        case JOB_MEDIC:
        case BUILD:
            return true;
        default:
            return false;
    }
}

bool eligible(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

std::string uidOf(Building* b) {
    InstanceID* iid = b->getInstanceID();
    return (iid != 0) ? iid->uid : std::string();
}

// O char ja tem cargo de torre apontando p/ ESTA torre? (chave 234 + subject)
bool hasTurretCargoFor(Character* c, const std::string& turretUid) {
    int n = c->getPermajobCount();
    if (n > GAR_MAX_SLOTS) {
        n = GAR_MAX_SLOTS;
    }
    for (int s = 0; s < n; ++s) {
        if (c->getPermajob(s) != MAN_A_TURRET_PLAYER_JOB) {
            continue;
        }
        const Tasker* tk = c->getPermajobData(s);
        if (tk == 0) {
            continue;
        }
        hand subj = tk->subject;
        if (!subj.isValid()) {
            continue;
        }
        Building* b = subj.getBuilding();
        if (b != 0 && uidOf(b) == turretUid) {
            return true;
        }
    }
    return false;
}

// Perfil do char p/ a guarnicao: candidato? quantos cargos de torre ja tem?
void profileChar(Character* c, bool& candidate, int& turretCargos) {
    candidate = true;
    turretCargos = 0;
    int n = c->getPermajobCount();
    if (n > GAR_MAX_SLOTS) {
        n = GAR_MAX_SLOTS;
    }
    for (int s = 0; s < n; ++s) {
        TaskType k = c->getPermajob(s);
        if (isNonGuardDuty(k)) {
            candidate = false; // produtor/medico: nao arrancar do posto dele
        }
        if (k == MAN_A_TURRET_PLAYER_JOB) {
            ++turretCargos;
        }
    }
}

int skillOf(Character* c, StatsEnumerated stat) {
    CharStats* st = c->getStats();
    if (st == 0) {
        return -1;
    }
    return static_cast<int>(st->getStat(stat, false));
}

struct TurretSpot {
    Building*   b;
    std::string uid;
    double      dist2; // ao centro (ordem estavel de preenchimento)
};

struct SpotOrder {
    bool operator()(const TurretSpot& a, const TurretSpot& c) const {
        if (a.dist2 != c.dist2) return a.dist2 < c.dist2;
        return a.uid < c.uid;
    }
};

} // namespace

void poc028GuarnicaoTick(GameWorld* world) {
    if (!core::pocEnv().garrison || world == 0) {
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
    if (!core::writeGateOpen(mode, fence)) {
        return; // silencioso: a rodada seguinte tenta de novo
    }
    // isFreeSlot le currentOperators (worker thread) -> rodada inteira exige
    // filas limpas (inv.21). Idempotente: adiar nao perde nada.
    if (!fence.threadsClear) {
        return;
    }

    // Ancora da base = town do 1o char valido (padrao das POCs).
    Character* anchor = 0;
    {
        lektor<Character*>& chars = pl->playerCharacters;
        uint32_t n = chars.size();
        if (n > GAR_MAX_CHARS) {
            n = GAR_MAX_CHARS;
        }
        for (uint32_t i = 0; i < n; ++i) {
            if (eligible(chars[i])) {
                anchor = chars[i];
                break;
            }
        }
    }
    if (anchor == 0) {
        return;
    }
    TownBase* town = anchor->getCurrentTownLocation();
    if (town == 0) {
        return;
    }

    // ---- 1) Torres da base (completas: com uid; em obra ainda nao tem) ----
    Ogre::Vector3 center = town->getPosition();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    std::vector<TurretSpot> spots;
    lektor<RootObject*> results;
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);
    for (uint32_t i = 0; i < results.size()
                      && static_cast<int>(spots.size()) < GAR_MAX_TURRETS; ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o);
        if (b->getBuildingClass() != BCTYPE_TURRET || b->getTown() != town) {
            continue;
        }
        UseableStuff* us = b->getUseableStuff();
        if (us == 0 || us->isBroken()) {
            continue;
        }
        TurretSpot ts;
        ts.b = b;
        ts.uid = uidOf(b);
        if (ts.uid.empty()) {
            continue; // em obra: sem uid, sem guarnicao ainda
        }
        Ogre::Vector3 bp = b->getPosition();
        double dx = bp.x - center.x, dy = bp.y - center.y, dz = bp.z - center.z;
        ts.dist2 = dx * dx + dy * dy + dz * dz;
        spots.push_back(ts);
    }
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
    if (spots.empty()) {
        if (throttle) {
            diag::log("GUARNICAO: nenhuma torre completa na base.");
            g_lastLog = g_round;
        }
        return;
    }
    std::sort(spots.begin(), spots.end(), SpotOrder());

    // ---- 2) Candidatos a guarda (sem cargos de producao/medico) ----
    Character* cand[GAR_MAX_CAND];
    int        candTurrets[GAR_MAX_CAND];
    int ncand = 0;
    {
        lektor<Character*>& chars = pl->playerCharacters;
        uint32_t n = chars.size();
        if (n > GAR_MAX_CHARS) {
            n = GAR_MAX_CHARS;
        }
        for (uint32_t i = 0; i < n && ncand < GAR_MAX_CAND; ++i) {
            Character* c = chars[i];
            if (!eligible(c)) {
                continue;
            }
            bool ok = false;
            int tc = 0;
            profileChar(c, ok, tc);
            if (!ok) {
                continue;
            }
            cand[ncand] = c;
            candTurrets[ncand] = tc;
            ++ncand;
        }
    }

    // ---- 3) Cobrir cada torre sem guarda: melhor skill, sem torre ainda ----
    // v1 = 1 guarda/torre e 1 torre/guarda (previsivel; papeis-fase2 sec.3).
    int emitted = 0, covered = 0, uncovered = 0;
    for (size_t si = 0; si < spots.size(); ++si) {
        TurretSpot& ts = spots[si];
        UseableStuff* us = ts.b->getUseableStuff();
        if (us == 0) {
            continue;
        }
        // Ja guarnecida? (algum candidato ou ocupante com cargo p/ ela)
        bool manned = false;
        for (int w = 0; w < ncand && !manned; ++w) {
            if (candTurrets[w] > 0 && hasTurretCargoFor(cand[w], ts.uid)) {
                manned = true;
            }
        }
        if (manned) {
            ++covered;
            continue;
        }
        if (emitted >= GAR_MAX_EMIT_PER) {
            ++uncovered; // proxima rodada continua daqui (idempotente)
            continue;
        }
        // Melhor candidato LIVRE de torre, pelo skill que a torre declara.
        StatsEnumerated stat = us->getStatUsed();
        int best = -1, bestSkill = -0x7fffffff;
        for (int w = 0; w < ncand; ++w) {
            if (candTurrets[w] > 0) {
                continue; // v1: 1 torre por guarda
            }
            int sk = skillOf(cand[w], stat);
            if (sk > bestSkill) {
                bestSkill = sk;
                best = w;
            }
        }
        if (best < 0) {
            ++uncovered;
            continue; // acabaram os guardas livres
        }
        Character* g = cand[best];
        {
            hand gh(g);
            if (!us->isFreeSlot(gh)) {
                ++uncovered;
                continue; // torre sem vaga real p/ este guarda
            }
        }
        // Verbo declarado pela torre, validado por constante nomeada (TUR-09).
        TaskType dt = us->getDefaultTask();
        if (dt != USE_TURRET) {
            ++uncovered;
            continue; // nao emitir em torre que nao declara USE_TURRET
        }
        Ogre::Vector3 tp = ts.b->getPosition();
        adapters::EmitResult r =
            adapters::emitAddPermajob(mode, fence, g, ts.b, dt, tp);
        if (r == adapters::EMIT_OK) {
            candTurrets[best] = 1; // consome o guarda nesta rodada
            ++emitted;
            ++covered;
            std::ostringstream s;
            s << "GUARNICAO: torre " << ts.uid << " <- \"" << g->getName()
              << "\" (skill=" << bestSkill << ")";
            diag::milestone(s.str());
        } else {
            ++uncovered;
        }
    }

    if (emitted > 0 || (throttle && uncovered > 0)) {
        std::ostringstream s;
        s << "GUARNICAO #" << g_round << ": " << covered << "/" << spots.size()
          << " torres guarnecidas (" << emitted << " novas nesta rodada, "
          << ncand << " candidatos)";
        if (uncovered > 0) {
            s << " -- " << uncovered << " sem guarda (sem candidato livre/vaga)";
        }
        diag::milestone(s.str());
        g_lastLog = g_round;
    }
}

} // namespace pocs
} // namespace ls
