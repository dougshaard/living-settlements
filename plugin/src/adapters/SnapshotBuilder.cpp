// Living Settlements -- adapters/SnapshotBuilder.cpp
// UNICA fronteira de leitura com KenshiLib. ASCII-only. So simbolos
// verificados no header sweep + padroes que ja compilam nas POCs.
#include "adapters/SnapshotBuilder.h"
#include "core/LsConfig.h"
#include "core/Diagnostics.h"
#include "core/Porters.h"

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
#include <kenshi/GameData.h>
#include <kenshi/Inventory.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <kenshi/util/TimeOfDay.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <cstdlib>   // free()
#include <vector>
#include <string>
#include <map>

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
        case OPERATE_MACHINERY:           return domain::WV_OPERATE_MACHINERY;
        case DELIVER_RESOURCES:           return domain::WV_DELIVER_RESOURCES;
        case BUILD:                       return domain::WV_BUILD;
        // Parte 5 (secao 9): conjunto de producao que getDefaultTask pode
        // retornar (refinaria/forno/gerador/storage). Constante nomeada.
        case FILL_MACHINE:                return domain::WV_FILL_MACHINE;
        case OPERATE_AUTOMATIC_MACHINERY: return domain::WV_OPERATE_AUTOMATIC;
        case COLLECT_OUTPUT_RESOURCE:     return domain::WV_COLLECT_OUTPUT;
        case EMPTY_MACHINE_OUTPUTS:       return domain::WV_EMPTY_OUTPUTS;
        case OPERATE_STORAGE:             return domain::WV_OPERATE_STORAGE;
        default:                          return domain::WV_UNKNOWN;
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

// Uma storage da base (deposito). O estoque REAL de um recurso vive aqui
// (soma sobre estas), NAO no buffer de input da maquina produtora.
bool isStorageFunction(BuildingFunction fn) {
    return fn == BF_RESOURCE_STORAGE || fn == BF_GENERAL_STORAGE;
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

// lektor<GameData*> preenchido pelo jogo -> vector<ItemNeed> (itemKey estavel
// via GameData::stringID). Buffer novo por chamada (o jogo aloca em .stuff);
// free() ao fim -- ponteiros tick-scoped, sem cache (invariante 11) [H3-V1].
void collectNeeds(lektor<GameData*>& src, std::vector<domain::ItemNeed>& out) {
    for (uint32_t i = 0; i < src.size(); ++i) {
        GameData* gd = src[i];
        if (gd != 0) {
            out.push_back(domain::ItemNeed(std::string("item:") + gd->stringID, 1));
        }
    }
    if (src.stuff != 0) {
        free(src.stuff);
        src.stuff = 0;
        src.count = 0;
        src.maxSize = 0;
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
    out.baseStock.clear();          // o passo de estoque usa += (mapa zerado)
    out.baseStockObserved = false;

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
    // Coletados no loop de postos p/ o passo de estoque-da-base (sec.4.4): as
    // storages (Inventory*) e os GameData* dos itens produzidos (tick-scoped,
    // nunca cacheados; inv.11). Ponteiros de predios/itens permanecem validos
    // apos free(results.stuff) -- este so libera o array de RootObject*.
    std::vector<Inventory*> storageInvs;
    std::map<std::string, GameData*> resourceGDs;
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
        // Classe C++ do predio (Building.h:249; carimbo de ctor @0x198, estavel
        // -- leitura de config, nao thread-mutada). Fase A: BCTYPE_TURRET
        // identifica torres no retrato (mapa-papeis sec.2/sec.4 eixo A).
        sv.buildingClass = static_cast<int>(b->getBuildingClass());
        Ogre::Vector3 p = b->getPosition();
        sv.posX = p.x; sv.posY = p.y; sv.posZ = p.z;

        // Deposito -> guarda o Inventory p/ o passo de estoque-da-base (sec.4.4).
        // Ponteiro tick-scoped; a LEITURA (getNumItems) e adiada p/ o ramo
        // thread-safe (inventario mutado em worker thread).
        if (LS_RES_READ_BASE_STOCK && isStorageFunction(fn)) {
            Inventory* inv = us->getInventory();
            if (inv != 0) {
                storageInvs.push_back(inv);
            }
        }

        sv.needsOperating = us->needsOperating;
        sv.operatorsMax = us->numOperatorsMax;
        sv.dontNeedWork = us->dontNeedWorkRightNow();
        sv.statUsed = static_cast<int>(us->getStatUsed());
        sv.broken = us->isBroken();
        sv.powerOk = !(us->isOutOfPower() > 0.0f); // isOutOfPower retorna float (deficit)

        // Verbo declarado pela estacao (getDefaultTask, secao 9): substitui
        // hardcodar OPERATE_MACHINERY. Leitura de config (nao mutada em thread)
        // -> incondicional. defaultTaskNative guarda o TaskType cru p/ o log
        // (fecha H1-verbo por observacao, sem escrever nada).
        TaskType dt = us->getDefaultTask();
        sv.defaultTaskNative = static_cast<int>(dt);
        sv.defaultVerb = verbFromTaskType(dt);

        // Membros mutados em worker thread: so ler se as filas estao limpas
        // (inv.21: rodada nao observada nao vira lacuna nem cede -- fecha F1).
        if (threadSafe) {
            collectOperators(us->currentOperators, sv.operatorsNow);
            sv.operatorsObserved = true;
            ProductionBuilding* pb = b->getProductionBuilding();
            if (pb != 0) {
                sv.productionState = prodFromNative(pb->productionState);
                sv.prodObserved = true;
                // Estoque -- o que a base precisa (banda morta nativa, secao 2.2).
                { lektor<GameData*> need; pb->getResourcesNeededBecauseEmpty(need);
                  collectNeeds(need, sv.needsCritical); }
                { lektor<GameData*> need; pb->getResourcesNeededBecauseNotFull(need);
                  collectNeeds(need, sv.needsTopup); }
                { lektor<GameData*> rid; pb->getItemsWeWantRidOf(rid, false);
                  collectNeeds(rid, sv.surplus); }
                // O QUE a estacao produz + riqueza do veio (desempate; [H4]).
                GameData* prod = pb->getProductionItemData();
                if (prod != 0) {
                    sv.producesItemKey = std::string("item:") + prod->stringID;
                    sv.producesItemName = prod->name;
                    sv.producesObserved = true;
                    // GameData* do item produzido -> chave do estoque-da-base.
                    // Conjunto pequeno (itens distintos produzidos); dedup por chave.
                    if (LS_RES_READ_BASE_STOCK) {
                        resourceGDs[sv.producesItemKey] = prod;
                    }
                }
                sv.veinRichness = pb->getMiningResourceLevel();
                // Inputs: amount/capacidade por ConsumptionItem [V] (fill
                // agregado por recurso -- banda morta / subida-observada sec.4).
                // GUARDA [H6] (CAUSA PROVAVEL DO HANG): um nci lixo (enorme)
                // travaria o loop. Producao real tem poucos inputs -> cap DURO.
                if (LS_RES_READ_INPUTS) {
                    int nci = pb->getNumConsumtionItems();
                    if (nci < 0) {
                        nci = 0;
                    }
                    if (nci > 64) {
                        nci = 64; // cap defensivo: nunca deixar o loop desgovernar
                    }
                    for (int ci = 0; ci < nci; ++ci) {
                        StorageBuilding::ConsumptionItem* cit = pb->getConsumtionItems(ci);
                        if (cit == 0) {
                            continue;
                        }
                        std::string ik;
                        if (cit->item != 0) {
                            ik = std::string("item:") + cit->item->stringID;
                        }
                        sv.inputs.push_back(domain::StockSlotView(
                            ik, static_cast<double>(cit->amount),
                            static_cast<double>(cit->maxCapacity)));
                    }
                }
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

    if (LS_TRACE_SNAP_PHASES) {
        diag::milestone("SNAP fase A: estacoes lidas (pos loop de postos)");
    }

    // ---- Estoque-da-base (sec.4.4): soma getNumItems() dos itens produzidos
    // sobre as storages. Inventario e mutado em worker thread -> so no ramo
    // thread-safe. NOVA superficie de iteracao nativa -> GUARDRAIL do hang:
    // ambos os lacos sao CAPADOS (storages por LS_RES_MAX_STORAGE_SCAN;
    // resourceGDs e naturalmente pequeno = itens distintos produzidos).
    if (threadSafe && LS_RES_READ_BASE_STOCK
        && !storageInvs.empty() && !resourceGDs.empty()) {
        std::size_t nInv = storageInvs.size();
        if (nInv > static_cast<std::size_t>(LS_RES_MAX_STORAGE_SCAN)) {
            nInv = static_cast<std::size_t>(LS_RES_MAX_STORAGE_SCAN);
        }
        // Semeia TODO item produzido com 0 -> "medido 0" (deposito vazio) fica
        // distinto de "nao medido" (recurso so consumido/importado, ausente do
        // mapa). O StockPolicy usa presenca-no-mapa como "ha sinal" (nao 0 duro).
        for (std::map<std::string, GameData*>::iterator it = resourceGDs.begin();
             it != resourceGDs.end(); ++it) {
            out.baseStock[it->first] = 0.0;
        }
        for (std::size_t si = 0; si < nInv; ++si) {
            Inventory* inv = storageInvs[si];
            if (inv == 0) {
                continue;
            }
            for (std::map<std::string, GameData*>::iterator it = resourceGDs.begin();
                 it != resourceGDs.end(); ++it) {
                int n = inv->getNumItems(it->second); // [V] Inventory.h:146 (const)
                if (n > 0) {
                    out.baseStock[it->first] += static_cast<double>(n);
                }
            }
        }
        out.baseStockObserved = true;
    }

    if (LS_TRACE_SNAP_PHASES) {
        diag::milestone("SNAP fase A2: estoque-da-base lido");
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
        w.declaredPorter = core::isPorter(c);

        int npj = c->getPermajobCount();
        for (int s = 0; s < npj; ++s) {
            domain::PermajobView pv;
            pv.verb = verbFromTaskType(c->getPermajob(s));
            pv.roleName = c->getPermajobName(s);
            // F8 (fecha o furo do servedBy): resolve o subject do cargo -> a
            // maquina. subject e um hand [V] (Tasker.h:146); hand::getBuilding()
            // [V] (hand.h:52) -> Building -> InstanceID.uid = StationId.
            // GATE: so cargos de TRABALHO reconhecidos (verb != UNKNOWN) -- em
            // cargos como curar/resgatar o subject NAO e maquina (evita
            // getBuilding em handle nao-predio). LS_F8_RESOLVE p/ bissecao.
            if (LS_F8_RESOLVE && pv.verb != domain::WV_UNKNOWN) {
                const Tasker* tk = c->getPermajobData(s);
                if (tk != 0) {
                    hand subj = tk->subject;
                    if (subj.isValid()) {
                        Building* mb = subj.getBuilding();
                        if (mb != 0) {
                            InstanceID* mid = mb->getInstanceID();
                            if (mid != 0) {
                                pv.roleMachineId = mid->uid;
                            }
                        }
                    }
                }
            }
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

        // Peso (inv.17): carga atual e capacidade. getTotalCarryWeight [V]
        // (Character.h:572) deriva do INVENTARIO do personagem, mutado em worker
        // thread -> so ler no ramo thread-safe (inv.21), igual ao estoque. Em
        // rodada thread-adiada, carryObserved=false -> loadRatio nao e confiavel
        // e nao exclui ninguem por peso. getStat(_MaxCarryWeight) [V] (Enums.h:692).
        if (threadSafe) {
            w.carryNow = c->getTotalCarryWeight();
            if (st != 0) {
                w.carryMax = st->getStat(_MaxCarryWeight, false);
            }
            w.carryObserved = true;
            // Fase A (medico): feridos pre-calculados pelo jogo. MedicalSystem e
            // POR VALOR em Character (getMedical devolve o membro; MedicalSystem.h
            // :223-224). Os floats sao escritos em fase THREADED (medicalUpdate/
            // periodicUpdate) -> SO neste ramo (inv.21), stale-tolerante.
            MedicalSystem* med = c->getMedical();
            if (med != 0) {
                w.firstAidFleshy = med->needsFirstAidScoreTotal_fleshy;
                w.firstAidRobot = med->needsFirstAidScoreTotal_robot;
                w.firstAidObserved = true;
            }
        }

        out.workers.push_back(w);
    }

    if (LS_TRACE_SNAP_PHASES) {
        diag::milestone("SNAP fase B: workers lidos (snapshot completo)");
    }

    return true;
}

} // namespace adapters
} // namespace ls
