// Living Settlements -- adapters/SnapshotBuilder.cpp
// UNICA fronteira de leitura com KenshiLib. ASCII-only. So simbolos
// verificados no header sweep + padroes que ja compilam nas POCs.
#include "adapters/SnapshotBuilder.h"
#include "core/LsConfig.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/CharStats.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Tasker.h>
#include <kenshi/Town.h>
#include <kenshi/InstanceID.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/Building/StorageBuilding.h>
#include <kenshi/Building/ProductionBuilding.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <kenshi/util/TimeOfDay.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <cstdlib>   // free()
#include <vector>
#include <string>

namespace ls {
namespace adapters {

namespace {

using domain::WorkerId;

Character* firstValidPlayerCharacter(GameWorld* world) {
    if (world == 0 || world->player == 0) {
        return 0;
    }
    lektor<Character*>& chars = world->player->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        if (chars[i] != 0) {
            return chars[i];
        }
    }
    return 0;
}

// TaskType nativo -> WorkVerb neutro, SEMPRE por constante nomeada
// (invariante 12; nunca o ordinal). So os verbos ja confirmados.
int verbFromTaskType(TaskType t) {
    switch (t) {
        case OPERATE_MACHINERY: return domain::WV_OPERATE_MACHINERY;
        case DELIVER_RESOURCES: return domain::WV_DELIVER_RESOURCES;
        case BUILD:             return domain::WV_BUILD;
        default:                return domain::WV_UNKNOWN;
    }
}

int prodFromNative(ProductionBuilding::ProductionState s) {
    switch (s) {
        case ProductionBuilding::PRODUCTION_NORMAL:     return domain::PROD_NORMAL;
        case ProductionBuilding::PRODUCTION_STARVED:    return domain::PROD_STARVED;
        case ProductionBuilding::PRODUCTION_FULL:       return domain::PROD_FULL;
        case ProductionBuilding::PRODUCTION_IMPOSSIBLE: return domain::PROD_IMPOSSIBLE;
        default:                                        return domain::PROD_UNKNOWN;
    }
}

// getProneState() nativo -> neutro (0 = ok; 1 = incapacitado). Apenas
// CRIPPLED/PLAYING_DEAD/KO contam como incapacitado (STAYING_LOW e ok).
int neutralProne(ProneState p) {
    switch (p) {
        case PS_CRIPPLED:
        case PS_PLAYING_DEAD:
        case PS_KO:
            return 1;
        default:
            return 0;
    }
}

// BuildingFunction nativo -> WorkClass neutro, por CONSTANTE NOMEADA.
// So producao recebe operador no nucleo (achado do 1o run do Marco 0).
int workClassOf(BuildingFunction fn) {
    switch (fn) {
        case BF_MINE:
        case BF_MINE_NATURAL:
        case BF_REFINERY:
        case BF_CRAFTING:
        case BF_ITEM_FURNACE:
        case BF_RESEARCH:
        case BF_GENERATOR:
            return domain::WC_PRODUCTION;
        case BF_TRAINING:
            return domain::WC_TRAINING;
        default:
            return domain::WC_OTHER;
    }
}

int hungerBandOf(MedicalSystem* med) {
    if (med == 0) {
        return domain::HUNGER_OK;
    }
    if (med->isHungerKO()) {
        return domain::HUNGER_KO;
    }
    if (med->isReallyHungry()) {
        return domain::HUNGER_REALLY_HUNGRY;
    }
    return domain::HUNGER_OK;
}

// Resolve o std::set<hand> de operadores para ids estaveis (getName).
// Template p/ nao precisar nomear o allocator do set (UB-safe: so lido
// quando threadReadsSafe garantiu quiescencia).
template <class SetT>
void collectOperators(const SetT& ops, std::vector<WorkerId>& out) {
    for (typename SetT::const_iterator it = ops.begin(); it != ops.end(); ++it) {
        hand h = *it;
        if (!h.isValid()) {
            continue;
        }
        Character* oc = h.getCharacter();
        if (oc != 0) {
            out.push_back(oc->getName());
        }
    }
}

bool isSelectedByPlayer(PlayerInterface* player, Character* c) {
    if (player == 0 || c == 0) {
        return false;
    }
    // Selecao singular (a unidade ativamente comandada). O conjunto
    // selectedCharacters (ogre_unordered_set) fica p/ um passo posterior.
    hand sel = player->selectedCharacter;
    if (sel.isValid() && sel.getCharacter() == c) {
        return true;
    }
    return false;
}

void addStatUnique(std::vector<int>& stats, int s) {
    if (s < 0) {
        return;
    }
    for (size_t i = 0; i < stats.size(); ++i) {
        if (stats[i] == s) {
            return;
        }
    }
    stats.push_back(s);
}

} // namespace

bool buildWorkSnapshot(GameWorld* world, bool threadSafe,
                       domain::WorldSnapshot& out) {
    out.workers.clear();
    out.stations.clear();
    out.readGateOpen = false;
    out.nowHours = 0.0;

    Character* anchor = firstValidPlayerCharacter(world);
    if (anchor == 0) {
        return false;
    }
    TownBase* town = anchor->getCurrentTownLocation();
    if (town == 0) {
        return false; // single-base: sem base, nada a coordenar
    }

    out.readGateOpen = true;
    out.nowHours = world->getTimeStamp_inGameHours().getTotalHours();

    // ---- Estacoes: enumeracao espacial ancorada no TownBase (POC-002) ----
    std::vector<int> statsUsed;
    lektor<RootObject*> results;
    Ogre::Vector3 center = town->getPosition();
    out.baseX = center.x; out.baseY = center.y; out.baseZ = center.z;
    out.baseRadius = town->getRadius();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);

    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* obj = results[i];
        if (obj == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(obj); // filtro BUILDING garante o tipo
        UseableStuff* us = b->getUseableStuff();
        if (us == 0) {
            continue; // so postos de trabalho entram no retrato
        }

        domain::StationView sv;
        InstanceID* iid = b->getInstanceID();
        if (iid != 0) {
            sv.id = iid->uid;
        }
        BuildingFunction fn = b->getSpecialFunction();
        sv.function = static_cast<int>(fn);
        sv.workClass = workClassOf(fn);
        Ogre::Vector3 p = b->getPosition();
        sv.posX = p.x; sv.posY = p.y; sv.posZ = p.z;

        sv.needsOperating = us->needsOperating;
        sv.operatorsMax = us->numOperatorsMax;
        sv.dontNeedWork = us->dontNeedWorkRightNow();
        sv.statUsed = static_cast<int>(us->getStatUsed());
        sv.broken = us->isBroken();
        sv.powerOk = !(us->isOutOfPower() > 0.0f); // isOutOfPower retorna float (deficit)

        // Membros mutados em worker thread: so ler se as filas estao limpas.
        if (threadSafe) {
            collectOperators(us->currentOperators, sv.operatorsNow);
            ProductionBuilding* pb = b->getProductionBuilding();
            if (pb != 0) {
                sv.productionState = prodFromNative(pb->productionState);
            }
        }

        addStatUnique(statsUsed, sv.statUsed);
        out.stations.push_back(sv);
    }
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }

    // ---- Workers: roster do jogador ----
    lektor<Character*>& chars = world->player->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        Character* c = chars[i];
        if (c == 0) {
            continue;
        }
        domain::WorkerView w;
        w.id = c->getName();
        w.name = w.id;
        w.isAnimal = (c->isAnimal() != 0);
        Ogre::Vector3 wp = c->getPosition();
        w.posX = wp.x; w.posY = wp.y; w.posZ = wp.z;

        CharBody* body = c->getBody();
        int prio = 0;
        if (body != 0) {
            w.isIdle = body->isIdle();
            Tasker* action = body->getCurrentAction();
            if (action != 0) {
                prio = static_cast<int>(action->priority);
            }
        }
        w.currentPriority = prio;
        w.underDirectOrder = (prio >= static_cast<int>(TP_OBEDIENCE));

        w.medical.isDead = c->isDead();
        w.medical.isUnconcious = c->isUnconcious();
        w.medical.proneState = neutralProne(c->getProneState());
        w.hungerBand = hungerBandOf(c->getMedical());
        w.canTakeOrders = c->canTakePlayerOrdersAtThisTime();
        w.selectedByPlayer = isSelectedByPlayer(world->player, c);

        int npj = c->getPermajobCount();
        for (int s = 0; s < npj; ++s) {
            domain::PermajobView pv;
            pv.verb = verbFromTaskType(c->getPermajob(s));
            pv.roleName = c->getPermajobName(s);
            // roleMachineId: subject->maquina e [H3] (nao resolvivel por hand);
            // fica vazio -- fallback via currentOperators (secao 2.1).
            w.permajobs.push_back(pv);
        }

        CharStats* st = c->getStats();
        if (st != 0) {
            for (size_t k = 0; k < statsUsed.size(); ++k) {
                int statId = statsUsed[k];
                float lvl = st->getStat(static_cast<StatsEnumerated>(statId), false);
                w.skills.push_back(domain::SkillLevel(statId, static_cast<int>(lvl)));
            }
        }

        out.workers.push_back(w);
    }

    return true;
}

} // namespace adapters
} // namespace ls
