// Living Settlements -- pocs/Poc029_Carregador.cpp
// CARREGADOR DIRIGIDO. ASCII-only. So simbolos verificados no header sweep:
//   Character::setDestination (via OrderEmitter::emitPreposition, POC-011)
//   Character::getInventory/getTotalCarryWeight        Character.h
//   Inventory::getAllItems/removeItemDontDestroy_returnsItem/addItem/
//     hasRoomForItem                                   Inventory.h:227/172/149/170
//   Item::quantity@0x12C / getItemWeightSingle / data  Item.h / RootObjectBase.h:76
//   ProductionBuilding::getResourcesNeededBecauseEmpty (padrao SnapshotBuilder)
//   GameWorld::getTimeStamp_inGameHours                (padrao SnapshotBuilder)
// Escrita de ORDEM so via OrderEmitter; a transferencia scriptada e a mutacao
// sancionada pela RISK-013 e roda INTEIRA num unico tick com a cerca aberta e
// filas de thread limpas (inv.21). Caps duros em todo laco nativo.
//
// CRITERIOS DE CONFIRM (escritos ANTES da sessao):
//   CONFIRM-HAUL-1 (coleta): itens saem da fonte e entram no carregador com
//     conservacao provada (saiu == entrou, contado nas duas pontas).
//   CONFIRM-HAUL-2 (entrega / POC-005): estoque do deposito de destino SOBE
//     na entrega, conservacao ok nas duas pontas.
//   CONFIRM-HAUL-3 (efeito de cadeia): a estacao faminta que motivou o haul
//     volta a produzir (o dono PERCEBE JOGANDO: Hive 19 para de fingir).
//   CONFIRM-HAUL-4 (degradacao): sem fonte transportavel -> linha de demanda
//     ("e preciso PRODUZIR X") e nenhuma escrita; falha de perna -> aborta,
//     libera reservas, cooldown (task_lifecycle FAILED).
#include "pocs/Poc029_Carregador.h"
#include "core/PocEnv.h"
#include "core/Porters.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "core/LsConfig.h"
#include "adapters/OrderEmitter.h"
#include "domain/ReservationManager.h"

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
#include <kenshi/Building/ProductionBuilding.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/GameData.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <cstdlib>   // free()
#include <string>
#include <vector>
#include <map>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// ---- Caps duros (guardrail do hang) e parametros da v1 ----
static const uint32_t HAUL_MAX_CHARS        = 512;
static const int      HAUL_MAX_ITEM_SCAN    = 512;  // itens varridos por inventario
static const int      HAUL_MAX_STACK_MOVES  = 16;   // stacks movidos por transferencia
static const int      HAUL_BATCH_MAX        = 5;    // REQ-LOG-004: lote seguro (unidades)
static const double   HAUL_MAX_LOAD_RATIO   = 0.80; // nunca lotar o inventario (deadlock
                                                    // de fome reportado pelo dono)
static const float    HAUL_ARRIVE_M         = 6.0f; // chegada = a menos de 6m do predio
                                                    // (POC-011 confirmou <2m ao ar livre;
                                                    // predio tem raio -> folga)
static const unsigned long HAUL_LEG_TIMEOUT   = 18; // rodadas (~3min) por perna
static const unsigned long HAUL_REEMIT_EVERY  = 6;  // re-emitir destino a cada N rodadas
static const int           HAUL_MAX_REEMITS   = 3;
static const unsigned long HAUL_FAIL_COOLDOWN = 30; // rodadas ate re-tentar a mesma tarefa
static const unsigned long HAUL_DONE_COOLDOWN = 6;  // pos-sucesso: deixa o estoque assentar
static const double        HAUL_LEASE_HOURS   = 6.0;// lease da reserva (horas de jogo)
static const int           HAUL_MAX_DEMANDS   = 16; // pares (estacao,item) avaliados/rodada

// ---- Plano do haul: SO ids estaveis (uid/nome/stringID) e floats. NENHUM
// ponteiro do jogo vive aqui (save-agnostico; re-resolucao a cada tick). ----
enum HaulPhase { HP_GO_SRC = 0, HP_GO_DST };

struct HaulPlan {
    bool        active;
    int         phase;          // HaulPhase
    std::string itemSid;        // GameData::stringID (identidade + procedencia)
    std::string itemName;       // rotulo humano (log)
    std::string srcUid, srcName;
    std::string dstUid, dstName;
    std::string demandUid, demandName; // estacao faminta que motivou (pull)
    hand        haulerHand;     // IDENTIDADE do carregador (ADR-015: referencia
                                // fraca do proprio jogo, index+serial). Nome NAO
                                // identifica: roster grande tem "Hive 23" em
                                // dobro (bug real 17/07: plano escolheu o de
                                // perto, rastreio re-resolvia o gemeo a 2km)
    std::string haulerName;     // so p/ log
    float       srcX, srcY, srcZ;
    float       dstX, dstY, dstZ;
    int         batch;          // unidades alvo desta viagem
    int         pickedUp;       // unidades a bordo (contadas na coleta)
    unsigned long legStart;     // rodada em que a perna comecou
    int         reEmits;
    std::string owner;          // dono logico das reservas ("haul:<seq>")
    HaulPlan() : active(false), phase(HP_GO_SRC),
                 srcX(0), srcY(0), srcZ(0), dstX(0), dstY(0), dstZ(0),
                 batch(0), pickedUp(0), legStart(0), reEmits(0) {}
};

HaulPlan      g_plan;
unsigned long g_round = 0;
unsigned long g_lastLog = 0;
unsigned long g_seq = 0;
bool          g_disabled = false;   // conservacao violada -> feicao morre na sessao
double        g_lastNow = -1.0;     // deteccao de rollback (relogio andou p/ tras)
ls::domain::ReservationManager g_res;
std::map<std::string, unsigned long> g_cooldown; // taskKey -> rodada liberada

bool eligible(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

// Carregador candidato (decisao do dono 17/07): SO quem foi DECLARADO na
// aba Carregadores -- a declaracao e do jogador (dir.11) e substitui o
// antigo filtro de "livre a 300m" (esperar sorte de ter gente perto e
// ruim; carregador declarado atravessa a base). Mantem os gates de
// autoridade: elegivel, nao selecionado, sem ordem direta, nao faminto.
bool porterAvailable(PlayerInterface* pl, Character* c) {
    if (!eligible(c) || !core::isPorter(c)) {
        return false;
    }
    if (pl != 0) {
        hand sel = pl->selectedCharacter;
        if (sel.isValid() && sel.getCharacter() == c) {
            return false;
        }
    }
    CharBody* body = c->getBody();
    if (body != 0) {
        Tasker* action = body->getCurrentAction();
        if (action != 0
            && static_cast<int>(action->priority) >= static_cast<int>(TP_OBEDIENCE)) {
            return false; // ordem direta do jogador em curso: sagrada
        }
    }
    MedicalSystem* med = c->getMedical();
    if (med != 0 && med->isReallyHungry()) {
        return false;
    }
    return true;
}

// Motivo de um DECLARADO nao estar disponivel (diagnostico; espelha
// porterAvailable). "" = disponivel.
const char* porterUnavailReason(PlayerInterface* pl, Character* c) {
    if (c == 0 || c->isDead()) {
        return "morto/ausente";
    }
    if (c->isUnconcious()) {
        return "KO";
    }
    if (!c->canTakePlayerOrdersAtThisTime()) {
        return "sem-ordens-agora";
    }
    if (pl != 0) {
        hand sel = pl->selectedCharacter;
        if (sel.isValid() && sel.getCharacter() == c) {
            return "selecionado-por-voce";
        }
    }
    CharBody* body = c->getBody();
    if (body != 0) {
        Tasker* action = body->getCurrentAction();
        if (action != 0
            && static_cast<int>(action->priority) >= static_cast<int>(TP_OBEDIENCE)) {
            return "sob-sua-ordem-direta";
        }
    }
    MedicalSystem* med = c->getMedical();
    if (med != 0 && med->isReallyHungry()) {
        return "faminto";
    }
    return "";
}

std::string uidOf(Building* b) {
    InstanceID* iid = b->getInstanceID();
    return (iid != 0) ? iid->uid : std::string();
}

double dist2(const Ogre::Vector3& a, const Ogre::Vector3& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

bool isStorageFunction(BuildingFunction fn) {
    return fn == BF_RESOURCE_STORAGE || fn == BF_GENERAL_STORAGE;
}

// Procedencia/identidade de item = stringID do GameData (compat por origem,
// I-31/I-32: funcao e id, nunca nome). Padrao provado na Poc026.
std::string sidOf(Item* it) {
    if (it == 0 || it->data == 0) {
        return std::string();
    }
    return it->data->stringID;
}

// Conta UNIDADES do item (por stringID) num inventario: soma Item::quantity
// [V, Item.h 0x12C] sobre getAllItems() [V, Inventory.h:227 -- referencia ao
// membro, NAO liberar]. Cap duro no scan.
int countBySid(Inventory* inv, const std::string& sid) {
    if (inv == 0 || sid.empty()) {
        return 0;
    }
    const lektor<Item*>& all = inv->getAllItems();
    uint32_t n = all.size();
    if (n > HAUL_MAX_ITEM_SCAN) {
        n = HAUL_MAX_ITEM_SCAN;
    }
    int total = 0;
    for (uint32_t i = 0; i < n; ++i) {
        Item* it = all[i];
        if (it != 0 && sidOf(it) == sid) {
            int q = it->quantity;
            total += (q > 0 ? q : 0);
        }
    }
    return total;
}

// Primeiro stack do item (por stringID) num inventario; 0 se ausente.
Item* findStackBySid(Inventory* inv, const std::string& sid) {
    if (inv == 0 || sid.empty()) {
        return 0;
    }
    const lektor<Item*>& all = inv->getAllItems();
    uint32_t n = all.size();
    if (n > HAUL_MAX_ITEM_SCAN) {
        n = HAUL_MAX_ITEM_SCAN;
    }
    for (uint32_t i = 0; i < n; ++i) {
        Item* it = all[i];
        if (it != 0 && sidOf(it) == sid && it->quantity > 0) {
            return it;
        }
    }
    return 0;
}

// Re-resolve um predio da base por uid (tick-scoped; NUNCA cacheado).
Building* buildingByUid(GameWorld* world, TownBase* town, const std::string& uid) {
    if (uid.empty()) {
        return 0;
    }
    Ogre::Vector3 center = town->getPosition();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    Building* found = 0;
    lektor<RootObject*> results;
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);
    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o);
        if (b->getTown() == town && uidOf(b) == uid) {
            found = b;
            break;
        }
    }
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
    return found;
}

std::string taskKeyOf(const std::string& demandUid, const std::string& sid) {
    return demandUid + "|" + sid;
}

bool inCooldown(const std::string& key) {
    std::map<std::string, unsigned long>::iterator it = g_cooldown.find(key);
    return it != g_cooldown.end() && g_round < it->second;
}

void setCooldown(const std::string& key, unsigned long rounds) {
    g_cooldown[key] = g_round + rounds;
}

// Estado terminal libera TUDO (task_lifecycle FAILED/CANCELLED; PRINC-005:
// falha nunca deixa reserva presa). cooldownKey vazio = sem cooldown.
void abortHaul(const std::string& reason, const std::string& cooldownKey) {
    if (!g_plan.owner.empty()) {
        g_res.releaseOwner(g_plan.owner);
    }
    if (!cooldownKey.empty()) {
        setCooldown(cooldownKey, HAUL_FAIL_COOLDOWN);
    }
    std::ostringstream s;
    s << "HAUL #" << g_seq << " ABORTADO (" << reason << "): \"" << g_plan.itemName
      << "\" " << g_plan.srcName << " -> " << g_plan.dstName
      << " carregador=\"" << g_plan.haulerName << "\"";
    if (g_plan.pickedUp > 0) {
        s << " | " << g_plan.pickedUp << " unidade(s) FICAM com o carregador "
          << "(dentro de inventario; nada se perde)";
    }
    s << " -- reservas liberadas; cooldown e re-derivacao do zero.";
    diag::milestone(s.str());
    g_plan = HaulPlan();
}

// Reset frio total (load/rollback/lifecycle): re-derivar do zero e o
// contrato (diretriz 15). Intencao nao sobrevive ao mundo mudar por fora.
void coldReset(const char* why) {
    if (g_plan.active || g_res.leaseCount() > 0) {
        std::ostringstream s;
        s << "HAUL: reset frio (" << why << ") -- plano e reservas descartados; "
          << "re-derivacao na proxima rodada.";
        diag::milestone(s.str());
    }
    g_plan = HaulPlan();
    g_res.clear();
    g_cooldown.clear();
}

// ---- Transferencia scriptada (RISK-013): move ate `want` unidades de
// `sid` de src para dst, num UNICO tick. Devolve quantas unidades moveu.
// REGRA DE OURO: um Item* removido NUNCA fica solto -- ou entra no destino,
// ou VOLTA para a origem (dropOnFail=true na devolucao como ultima linha de
// defesa: chao conserva, destruir nunca). weightGuard: respeita a folga de
// carga de quem recebe (so faz sentido quando o destino e um personagem). ----
int scriptedTransfer(Inventory* src, Inventory* dst, const std::string& sid,
                     int want, Character* weightGuard) {
    if (src == 0 || dst == 0 || want <= 0) {
        return 0;
    }
    double carryNow = 0.0, carryMax = 0.0;
    if (weightGuard != 0) {
        carryNow = weightGuard->getTotalCarryWeight();
        CharStats* st = weightGuard->getStats();
        if (st != 0) {
            carryMax = st->getStat(_MaxCarryWeight, false);
        }
    }
    int moved = 0;
    for (int guard = 0; guard < HAUL_MAX_STACK_MOVES && moved < want; ++guard) {
        Item* it = findStackBySid(src, sid);
        if (it == 0) {
            break; // origem esgotou
        }
        int take = it->quantity;
        if (take > want - moved) {
            take = want - moved;
        }
        // REQ-LOG-004 / anti-deadlock: nunca passar da fracao segura de carga.
        if (weightGuard != 0 && carryMax > 0.0) {
            double unit = static_cast<double>(it->getItemWeightSingle());
            if (unit > 0.0) {
                double room = carryMax * HAUL_MAX_LOAD_RATIO - carryNow;
                int fit = static_cast<int>(room / unit);
                if (fit < take) {
                    take = fit;
                }
                if (take <= 0) {
                    break; // sem folga de peso: lote encerra aqui
                }
                carryNow += unit * static_cast<double>(take);
            }
        }
        if (take <= 0) {
            break;
        }
        // removeItemDontDestroy_returnsItem [V Inventory.h:172]: tira `take`
        // do stack sem destruir; returnCopyIfSomeLeft=true garante um Item*
        // representando o que saiu mesmo em remocao parcial.
        Item* removed = src->removeItemDontDestroy_returnsItem(it, take, true);
        if (removed == 0) {
            break; // nada saiu -> nada a devolver; para o lote
        }
        int got = removed->quantity;
        if (got <= 0) {
            got = take; // defensivo: confia no pedido se o campo vier zerado
        }
        // addItem [V Inventory.h:149] dropOnFail=false destroyOnFail=false:
        // falhou = item continua conosco -> DEVOLVER a origem.
        if (!dst->addItem(removed, got, false, false)) {
            if (!src->addItem(removed, got, false, false)) {
                // Ultima linha de defesa: devolve com dropOnFail=true (o jogo
                // solta no chao junto a origem -- conservado, nunca destruido).
                src->addItem(removed, got, true, false);
                diag::error("HAUL: devolucao a origem falhou; item posto no "
                            "chao junto a fonte (conservado).");
            }
            break; // destino recusou (cheio/incompativel): lote encerra
        }
        moved += got;
    }
    return moved;
}

// ---- Planejamento (DISCOVERED -> READY -> RESERVED -> ASSIGNED): pull
// puro (REQ-LOG-001): a demanda nasce da estacao faminta. ----
struct DemandCand {
    std::string stationUid, stationName;
    float       sx, sy, sz;
    std::string itemSid, itemName;
    double      d2Center; // ordem deterministica (sem flip-flop)
};

struct SourceCand {
    std::string uid, name;
    float       x, y, z;
    int         count;
    bool        surplus; // a fonte DECLARA excedente do item (getItemsWeWantRidOf)
};

struct DestCand {
    std::string uid, name;
    float       x, y, z;
    double      d2Demand;
    int         tier;    // 2 = TIPADO p/ o item; 1 = ja guarda; 0 = so espaco
};

// Deposito TIPADO para o item: alguma secao com veryLimitedSlot NAO-vazio
// aceita este GameData -- e um armazem FEITO para ele (sinal de tipo valido
// mesmo com estoque zero, que e exatamente quando o carregador trabalha).
// getAllSections/getVeryLimitedSlot retornam REFERENCIA a membro (nao liberar);
// isLimitedSlotCompatible [V] Inventory.h:75. Cap duro no scan.
bool typedForItem(Inventory* inv, GameData* gd) {
    if (inv == 0 || gd == 0) {
        return false;
    }
    lektor<InventorySection*>& secs = inv->getAllSections();
    uint32_t n = secs.size();
    if (n > 32) {
        n = 32;
    }
    for (uint32_t i = 0; i < n; ++i) {
        InventorySection* s = secs[i];
        if (s != 0 && s->getVeryLimitedSlot().size() > 0
            && s->isLimitedSlotCompatible(gd)) {
            return true;
        }
    }
    return false;
}

// A estacao declara o item como excedente? (getItemsWeWantRidOf; padrao de
// leitura+free do SnapshotBuilder). Fonte com excedente e prioridade: tirar
// dali AJUDA o produtor; tirar do buffer de outro consumidor e ultimo caso
// (evidencia 16/07: v1 drenou a agua da fazenda de cactos p/ a de trigo).
bool declaresSurplus(Building* b, const std::string& sid) {
    ProductionBuilding* pb = b->getProductionBuilding();
    if (pb == 0) {
        return false;
    }
    bool found = false;
    lektor<GameData*> rid;
    pb->getItemsWeWantRidOf(rid, false);
    for (uint32_t i = 0; i < rid.size(); ++i) {
        GameData* gd = rid[i];
        if (gd != 0 && sid == gd->stringID) {
            found = true;
            break;
        }
    }
    if (rid.stuff != 0) {
        free(rid.stuff);
        rid.stuff = 0;
        rid.count = 0;
        rid.maxSize = 0;
    }
    return found;
}

bool planHaul(GameWorld* world, PlayerInterface* pl, TownBase* town,
              core::CoordMode mode, const core::WriteFence& fence,
              double now, bool throttle) {
    Ogre::Vector3 center = town->getPosition();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    // Uma unica varredura espacial; ponteiros validos SO neste tick.
    lektor<RootObject*> results;
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);

    // 1) DEMANDAS: estacoes com falta CRITICA (banda nativa; pull REQ-LOG-001).
    std::vector<DemandCand> demands;
    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o);
        if (b->getTown() != town) {
            continue;
        }
        ProductionBuilding* pb = b->getProductionBuilding();
        if (pb == 0) {
            continue;
        }
        lektor<GameData*> need;
        pb->getResourcesNeededBecauseEmpty(need);
        for (uint32_t k = 0; k < need.size()
                          && static_cast<int>(demands.size()) < HAUL_MAX_DEMANDS; ++k) {
            GameData* gd = need[k];
            if (gd == 0) {
                continue;
            }
            DemandCand dc;
            dc.stationUid = uidOf(b);
            dc.stationName = b->getName();
            Ogre::Vector3 p = b->getPosition();
            dc.sx = p.x; dc.sy = p.y; dc.sz = p.z;
            dc.itemSid = gd->stringID;
            dc.itemName = gd->name;
            dc.d2Center = dist2(p, center);
            if (!dc.stationUid.empty() && !dc.itemSid.empty()
                && !inCooldown(taskKeyOf(dc.stationUid, dc.itemSid))) {
                demands.push_back(dc);
            }
        }
        if (need.stuff != 0) {
            free(need.stuff);
            need.stuff = 0;
            need.count = 0;
            need.maxSize = 0;
        }
        if (static_cast<int>(demands.size()) >= HAUL_MAX_DEMANDS) {
            break;
        }
    }

    if (demands.empty()) {
        if (results.stuff != 0) {
            free(results.stuff);
            results.stuff = 0; results.count = 0; results.maxSize = 0;
        }
        if (throttle) {
            diag::log("HAUL: nenhuma falta critica fora de cooldown -- nada a "
                      "transportar nesta rodada.");
            g_lastLog = g_round;
        }
        return false;
    }

    // Ordem deterministica: mais perto do centro primeiro (mesma demanda gera
    // o mesmo plano em todo tick -> sem flip-flop).
    for (size_t a = 0; a + 1 < demands.size(); ++a) {
        for (size_t b2 = a + 1; b2 < demands.size(); ++b2) {
            bool swap = demands[b2].d2Center < demands[a].d2Center
                || (demands[b2].d2Center == demands[a].d2Center
                    && demands[b2].stationUid < demands[a].stationUid);
            if (swap) {
                DemandCand tmp = demands[a];
                demands[a] = demands[b2];
                demands[b2] = tmp;
            }
        }
    }

    // 2) Para cada demanda: FONTE nao-deposito com o item + DESTINO deposito
    // com espaco. Deposito->deposito fica FORA da v1: e a oscilacao nativa
    // que o dono condenou (emenda 4.1); se o item ja esta num deposito, o
    // operador nativo alcanca -- transportar de novo nao muda nada.
    for (size_t di = 0; di < demands.size(); ++di) {
        DemandCand& dc = demands[di];
        Ogre::Vector3 dpos(dc.sx, dc.sy, dc.sz);
        SourceCand src; src.count = 0; src.surplus = false;
        DestCand dst; dst.d2Demand = -1.0; dst.tier = 0;
        int unitsInStorages = 0;
        GameData* itemGD = 0;

        for (uint32_t i = 0; i < results.size(); ++i) {
            RootObject* o = results[i];
            if (o == 0) {
                continue;
            }
            Building* b = static_cast<Building*>(o);
            if (b->getTown() != town) {
                continue;
            }
            std::string uid = uidOf(b);
            if (uid.empty()) {
                continue; // em obra: sem uid, fora
            }
            UseableStuff* us = b->getUseableStuff();
            if (us == 0) {
                continue;
            }
            Inventory* inv = us->getInventory();
            if (inv == 0) {
                continue;
            }
            bool storage = isStorageFunction(b->getSpecialFunction());
            int have = countBySid(inv, dc.itemSid);
            if (itemGD == 0 && have > 0) {
                Item* sample = findStackBySid(inv, dc.itemSid);
                if (sample != 0) {
                    itemGD = sample->data; // tick-scoped; so p/ hasRoomForItem
                }
            }
            if (storage) {
                unitsInStorages += have;
            } else if (have > 0 && uid != dc.stationUid) {
                // Melhor fonte por CAMADAS: excedente declarado vence sempre
                // (tirar dali ajuda o produtor); dentro da camada, mais
                // unidades; empate = mais perto da demanda.
                Ogre::Vector3 p = b->getPosition();
                bool sur = declaresSurplus(b, dc.itemSid);
                bool better;
                if (sur != src.surplus) {
                    better = sur;
                } else {
                    better = have > src.count
                        || (have == src.count && !src.uid.empty()
                            && dist2(p, dpos) < dist2(Ogre::Vector3(src.x, src.y, src.z), dpos));
                }
                if (better) {
                    src.uid = uid;
                    src.name = b->getName();
                    src.x = p.x; src.y = p.y; src.z = p.z;
                    src.count = have;
                    src.surplus = sur;
                }
            }
        }
        if (src.count <= 0) {
            // CONFIRM-HAUL-4 / JANELA DE DEMANDAS v0 (diretriz 13): dizer o que
            // RESOLVE, nao so o que falta.
            std::ostringstream s;
            s << "HAUL DEMANDA: \"" << dc.itemName << "\" em falta CRITICA na "
              << "estacao \"" << dc.stationName << "\" (" << dc.stationUid << ") e ";
            if (unitsInStorages > 0) {
                s << unitsInStorages << " unidade(s) ja estao em deposito -- o "
                  << "operador nativo alcanca; transporte nao resolve (v1 nao "
                  << "faz deposito->deposito). Se ele nao buscar, e outra lacuna.";
            } else {
                s << "NAO ha fonte transportavel na base -- e preciso PRODUZIR/"
                  << "minerar \"" << dc.itemName << "\" (janela de demandas v0).";
            }
            diag::milestone(s.str());
            setCooldown(taskKeyOf(dc.stationUid, dc.itemSid), HAUL_FAIL_COOLDOWN);
            continue;
        }

        // Destino: deposito com ESPACO para o item (o motor julga a
        // compatibilidade via hasRoomForItem), o mais perto da demanda.
        for (uint32_t i = 0; i < results.size(); ++i) {
            RootObject* o = results[i];
            if (o == 0) {
                continue;
            }
            Building* b = static_cast<Building*>(o);
            if (b->getTown() != town
                || !isStorageFunction(b->getSpecialFunction())) {
                continue;
            }
            std::string uid = uidOf(b);
            if (uid.empty() || uid == src.uid) {
                continue;
            }
            UseableStuff* us = b->getUseableStuff();
            if (us == 0 || us->isBroken()) {
                continue;
            }
            Inventory* inv = us->getInventory();
            if (inv == 0 || itemGD == 0 || !inv->hasRoomForItem(itemGD)) {
                continue;
            }
            // Tres camadas: armazem TIPADO p/ o item (secao limitada
            // compativel; vale mesmo VAZIO) > deposito que ja guarda o item
            // > qualquer um com espaco. hasRoomForItem sozinho aceita secao
            // generica de qualquer movel (evidencia 16/07: agua em armario
            // de besta). Dentro da camada, o mais perto da demanda.
            int tier = 0;
            if (typedForItem(inv, itemGD)) {
                tier = 2;
            } else if (countBySid(inv, dc.itemSid) > 0) {
                tier = 1;
            }
            Ogre::Vector3 p = b->getPosition();
            double d2 = dist2(p, dpos);
            bool better;
            if (dst.d2Demand < 0.0) {
                better = true;
            } else if (tier != dst.tier) {
                better = tier > dst.tier;
            } else {
                better = d2 < dst.d2Demand;
            }
            if (better) {
                dst.uid = uid;
                dst.name = b->getName();
                dst.x = p.x; dst.y = p.y; dst.z = p.z;
                dst.d2Demand = d2;
                dst.tier = tier;
            }
        }
        if (dst.d2Demand < 0.0) {
            std::ostringstream s;
            s << "HAUL DEMANDA: ha " << src.count << "x \"" << dc.itemName
              << "\" em \"" << src.name << "\" mas NENHUM deposito com espaco "
              << "compativel -- e preciso mais armazenamento (janela de "
              << "demandas v0).";
            diag::milestone(s.str());
            setCooldown(taskKeyOf(dc.stationUid, dc.itemSid), HAUL_FAIL_COOLDOWN);
            continue;
        }

        // 3) Carregador: o DECLARADO disponivel mais perto da fonte (ocioso
        // preferido). Sem trava de distancia: declarado atravessa a base.
        Character* hauler = 0;
        double bestScore = 0.0;
        Ogre::Vector3 spos(src.x, src.y, src.z);
        {
            lektor<Character*>& chars = pl->playerCharacters;
            uint32_t n = chars.size();
            if (n > HAUL_MAX_CHARS) {
                n = HAUL_MAX_CHARS;
            }
            for (uint32_t i = 0; i < n; ++i) {
                Character* c = chars[i];
                if (!porterAvailable(pl, c)) {
                    continue;
                }
                double d2 = dist2(c->getPosition(), spos);
                bool idle = false;
                CharBody* body = c->getBody();
                if (body != 0) {
                    idle = body->isIdle();
                }
                double score = d2 + (idle ? 0.0 : 1.0e8); // ocioso ganha sempre
                if (hauler == 0 || score < bestScore) {
                    hauler = c;
                    bestScore = score;
                }
            }
        }
        if (hauler == 0) {
            if (throttle) {
                if (core::porterCount() == 0) {
                    diag::milestone("HAUL: demanda pronta mas NENHUM carregador "
                                    "DECLARADO -- abra o painel > Carregadores e "
                                    "escolha quem transporta (janela de demandas).");
                } else {
                    // CENSO: quantos declarados o motor RECONHECE (isPorter) e,
                    // dos reconhecidos, o motivo de cada um nao estar livre. Se
                    // reconhecidos < declarados, sobrou identidade que nao casa
                    // (char saiu do mundo) -- visivel aqui.
                    int recognized = 0, detail = 0;
                    std::ostringstream why;
                    lektor<Character*>& cc = pl->playerCharacters;
                    uint32_t nn = cc.size();
                    if (nn > HAUL_MAX_CHARS) {
                        nn = HAUL_MAX_CHARS;
                    }
                    for (uint32_t i = 0; i < nn; ++i) {
                        Character* c = cc[i];
                        if (c == 0 || !core::isPorter(c)) {
                            continue;
                        }
                        ++recognized;
                        const char* r = porterUnavailReason(pl, c);
                        if (r[0] != '\0' && detail < 8) {
                            why << (detail ? ", " : "") << "\"" << c->getName()
                                << "\"=" << r;
                            ++detail;
                        }
                    }
                    std::ostringstream s;
                    s << "HAUL: demanda pronta mas nenhum carregador livre. "
                      << core::porterCount() << " declarado(s), " << recognized
                      << " reconhecido(s); motivos: "
                      << (why.str().empty() ? std::string("(todos livres?? anomalia)")
                                            : why.str());
                    diag::milestone(s.str());
                }
                g_lastLog = g_round;
            }
            if (results.stuff != 0) {
                free(results.stuff);
                results.stuff = 0; results.count = 0; results.maxSize = 0;
            }
            return false; // sem cooldown: gente livre muda a cada rodada
        }

        // 4) RESERVAS atomicas (REQ-LOG-002/003; ADR-015): item na fonte +
        // capacidade do destino + o proprio carregador, tudo-ou-nada.
        int batch = src.count < HAUL_BATCH_MAX ? src.count : HAUL_BATCH_MAX;
        ++g_seq;
        std::ostringstream ow;
        ow << "haul:" << g_seq;
        std::string owner = ow.str();
        {
            std::vector<ls::domain::ReservationRequest> reqs;
            std::string itemRes = "item:" + dc.itemSid + "@" + src.uid;
            std::string capRes = "cap:" + dst.uid;
            std::string wrkRes = "worker:" + hauler->getName();
            g_res.setPhysical(itemRes, src.count);
            g_res.setPhysical(capRes, 1);
            g_res.setPhysical(wrkRes, 1);
            double expiry = now + HAUL_LEASE_HOURS;
            reqs.push_back(ls::domain::ReservationRequest(itemRes, owner, batch, expiry));
            reqs.push_back(ls::domain::ReservationRequest(capRes, owner, 1, expiry));
            reqs.push_back(ls::domain::ReservationRequest(wrkRes, owner, 1, expiry));
            if (!g_res.acquireAtomic(reqs, now)) {
                diag::log("HAUL: reserva atomica NEGADA (recurso ja tomado) -- "
                          "proxima demanda.");
                setCooldown(taskKeyOf(dc.stationUid, dc.itemSid), HAUL_DONE_COOLDOWN);
                continue;
            }
        }

        // 5) ASSIGNED -> EXECUTING: rumo a fonte (verbo provado POC-011).
        adapters::EmitResult r =
            adapters::emitPreposition(mode, fence, hauler, spos);
        if (r != adapters::EMIT_OK) {
            g_res.releaseOwner(owner);
            std::ostringstream s;
            s << "HAUL: emissao bloqueada (" << adapters::emitResultName(r)
              << ") -- re-tenta na proxima rodada.";
            diag::log(s.str());
            if (results.stuff != 0) {
                free(results.stuff);
                results.stuff = 0; results.count = 0; results.maxSize = 0;
            }
            return false;
        }

        g_plan.active = true;
        g_plan.phase = HP_GO_SRC;
        g_plan.itemSid = dc.itemSid;
        g_plan.itemName = dc.itemName;
        g_plan.srcUid = src.uid;   g_plan.srcName = src.name;
        g_plan.dstUid = dst.uid;   g_plan.dstName = dst.name;
        g_plan.demandUid = dc.stationUid;
        g_plan.demandName = dc.stationName;
        g_plan.haulerHand = hand(hauler);
        g_plan.haulerName = hauler->getName();
        g_plan.srcX = src.x; g_plan.srcY = src.y; g_plan.srcZ = src.z;
        g_plan.dstX = dst.x; g_plan.dstY = dst.y; g_plan.dstZ = dst.z;
        g_plan.batch = batch;
        g_plan.pickedUp = 0;
        g_plan.legStart = g_round;
        g_plan.reEmits = 0;
        g_plan.owner = owner;

        std::ostringstream s;
        s << "HAUL #" << g_seq << " PLANEJADO: " << batch << "x \"" << dc.itemName
          << "\" [" << dc.itemSid << "] de \"" << src.name << "\" (" << src.uid
          << ", " << src.count << " disponiveis"
          << (src.surplus ? ", EXCEDENTE declarado" : ", buffer de producao")
          << ") -> deposito \"" << dst.name << "\" (" << dst.uid
          << (dst.tier == 2 ? ", TIPADO p/ o item"
                            : (dst.tier == 1 ? ", ja guarda o item"
                                             : ", so tem espaco"))
          << ") | demanda: \"" << dc.stationName
          << "\" | carregador: \"" << g_plan.haulerName
          << "\" | reservas OK; a caminho da fonte.";
        diag::milestone(s.str());
        if (results.stuff != 0) {
            free(results.stuff);
            results.stuff = 0; results.count = 0; results.maxSize = 0;
        }
        return true;
    }

    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0; results.count = 0; results.maxSize = 0;
    }
    return false;
}

} // namespace

void poc029CarregadorTick(GameWorld* world) {
    if (world == 0 || g_disabled) {
        return;
    }
    const core::PocEnvState& env = core::pocEnv();
    // Haul ATIVO sempre e processado (nunca deixar reserva/carga pendurada);
    // ocioso, so age com o laco ligado ou 1 ciclo pedido no painel.
    if (!g_plan.active && !env.haul && !env.haulOnce) {
        return;
    }
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        coldReset("lifecycle fora de OBSERVE_AND_ACT");
        return;
    }
    PlayerInterface* pl = world->player;
    if (pl == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    ++g_round;
    bool throttle = (g_round - g_lastLog) >= 6;
    // Transferencia scriptada le e MUTA inventarios (worker thread os toca):
    // a rodada inteira exige cerca aberta E filas limpas (inv.21). Adiar
    // nao perde nada (idempotente).
    if (!core::writeGateOpen(mode, fence) || !fence.threadsClear) {
        return;
    }

    double now = world->getTimeStamp_inGameHours().getTotalHours();
    if (g_lastNow >= 0.0 && now + 0.001 < g_lastNow) {
        coldReset("relogio do jogo voltou (rollback/load)");
        g_lastNow = now;
        return; // proxima rodada re-deriva do zero
    }
    g_lastNow = now;
    g_res.expire(now);
    if (g_plan.active && !g_res.ownerHasLeases(g_plan.owner)) {
        abortHaul("lease expirado", taskKeyOf(g_plan.demandUid, g_plan.itemSid));
        return;
    }

    // Ancora da base (padrao das POCs).
    Character* anchor = 0;
    {
        lektor<Character*>& chars = pl->playerCharacters;
        uint32_t n = chars.size();
        if (n > HAUL_MAX_CHARS) {
            n = HAUL_MAX_CHARS;
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

    // ---- IDLE: procurar demanda e planejar ----
    if (!g_plan.active) {
        bool planned = planHaul(world, pl, town, mode, fence, now, throttle);
        if (env.haulOnce) {
            core::pocEnvMutable().haulOnce = false; // pedido do painel atendido
            if (!planned) {
                diag::milestone("HAUL (1 ciclo): nenhuma viagem planejada nesta "
                                "rodada -- ver linhas HAUL acima pelo motivo.");
            }
        }
        return;
    }

    // ---- Haul ativo: re-resolver o carregador pelo HAND (identidade exata;
    // ponteiro tick-scoped, nunca guardado) ----
    Character* w = 0;
    if (g_plan.haulerHand.isValid()) {
        w = g_plan.haulerHand.getCharacter();
    }
    if (w == 0 || !eligible(w)) {
        abortHaul("carregador indisponivel (sumiu/KO/morto)",
                  taskKeyOf(g_plan.demandUid, g_plan.itemSid));
        return;
    }
    Ogre::Vector3 wp = w->getPosition();
    Ogre::Vector3 target = (g_plan.phase == HP_GO_SRC)
        ? Ogre::Vector3(g_plan.srcX, g_plan.srcY, g_plan.srcZ)
        : Ogre::Vector3(g_plan.dstX, g_plan.dstY, g_plan.dstZ);
    double d2 = dist2(wp, target);
    double arrive2 = static_cast<double>(HAUL_ARRIVE_M) * HAUL_ARRIVE_M;

    if (d2 > arrive2) {
        unsigned long onLeg = g_round - g_plan.legStart;
        if (onLeg > HAUL_LEG_TIMEOUT) {
            abortHaul("timeout de perna (nao chegou)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        if (onLeg > 0 && (onLeg % HAUL_REEMIT_EVERY) == 0
            && g_plan.reEmits < HAUL_MAX_REEMITS) {
            // GOAP ocioso pode ter puxado o char: re-emitir converge.
            if (adapters::emitPreposition(mode, fence, w, target)
                    == adapters::EMIT_OK) {
                ++g_plan.reEmits;
                std::ostringstream s;
                s << "HAUL #" << g_seq << ": destino re-emitido ("
                  << g_plan.reEmits << "/" << HAUL_MAX_REEMITS << ")";
                diag::log(s.str());
            }
        } else if (throttle) {
            std::ostringstream s;
            s << "HAUL #" << g_seq << " ("
              << (g_plan.phase == HP_GO_SRC ? "rumo a fonte" : "rumo ao deposito")
              << "): dist2=" << d2 << " rodada-da-perna=" << onLeg;
            diag::log(s.str());
            g_lastLog = g_round;
        }
        return;
    }

    // ---- Chegou. Transferencia scriptada NA MESMA rodada (cerca ja aberta). ----
    if (g_plan.phase == HP_GO_SRC) {
        Building* src = buildingByUid(world, town, g_plan.srcUid);
        if (src == 0 || src->getUseableStuff() == 0) {
            abortHaul("fonte nao re-resolvida (mundo mudou)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        Inventory* srcInv = src->getUseableStuff()->getInventory();
        Inventory* haulInv = w->getInventory();
        if (srcInv == 0 || haulInv == 0) {
            abortHaul("inventario ausente na coleta",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        int srcBefore = countBySid(srcInv, g_plan.itemSid);
        int haulBefore = countBySid(haulInv, g_plan.itemSid);
        if (srcBefore <= 0) {
            abortHaul("fonte esvaziou antes da coleta (corrida)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        int moved = scriptedTransfer(srcInv, haulInv, g_plan.itemSid,
                                     g_plan.batch, w);
        int srcAfter = countBySid(srcInv, g_plan.itemSid);
        int haulAfter = countBySid(haulInv, g_plan.itemSid);
        int outSrc = srcBefore - srcAfter;
        int inHaul = haulAfter - haulBefore;
        if (outSrc != inHaul) {
            std::ostringstream s;
            s << "HAUL CONSERVACAO VIOLADA na coleta: fonte " << srcBefore
              << "->" << srcAfter << " (saiu " << outSrc << ") vs carregador "
              << haulBefore << "->" << haulAfter << " (entrou " << inHaul
              << "). CARREGADOR DESATIVADO nesta sessao (degraded-safe); "
              << "reservas liberadas. Investigar antes de religar.";
            diag::error(s.str());
            g_res.releaseOwner(g_plan.owner);
            g_plan = HaulPlan();
            g_disabled = true;
            return;
        }
        if (moved <= 0 || inHaul <= 0) {
            abortHaul("coleta nao moveu nada (sem folga de peso/stack)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        g_plan.pickedUp = inHaul;
        g_plan.phase = HP_GO_DST;
        g_plan.legStart = g_round;
        g_plan.reEmits = 0;
        adapters::EmitResult r = adapters::emitPreposition(
            mode, fence, w, Ogre::Vector3(g_plan.dstX, g_plan.dstY, g_plan.dstZ));
        std::ostringstream s;
        s << "HAUL #" << g_seq << " COLETA OK: " << inHaul << "x \""
          << g_plan.itemName << "\" (fonte " << srcBefore << "->" << srcAfter
          << ", carregador " << haulBefore << "->" << haulAfter
          << "; CONSERVACAO PROVADA) -- CONFIRM-HAUL-1. Rumo ao deposito \""
          << g_plan.dstName << "\" (emissao=" << adapters::emitResultName(r)
          << ").";
        diag::milestone(s.str());
        return;
    }

    // HP_GO_DST: entregar.
    {
        Building* dst = buildingByUid(world, town, g_plan.dstUid);
        if (dst == 0 || dst->getUseableStuff() == 0) {
            abortHaul("destino nao re-resolvido (mundo mudou); carga fica com "
                      "o carregador",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        Inventory* dstInv = dst->getUseableStuff()->getInventory();
        Inventory* haulInv = w->getInventory();
        if (dstInv == 0 || haulInv == 0) {
            abortHaul("inventario ausente na entrega",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        int haulBefore = countBySid(haulInv, g_plan.itemSid);
        int dstBefore = countBySid(dstInv, g_plan.itemSid);
        if (haulBefore <= 0) {
            abortHaul("carregador chegou sem a carga (corrida)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        int toDrop = haulBefore < g_plan.pickedUp ? haulBefore : g_plan.pickedUp;
        int moved = scriptedTransfer(haulInv, dstInv, g_plan.itemSid,
                                     toDrop, 0 /*sem trava de peso: deposito*/);
        int haulAfter = countBySid(haulInv, g_plan.itemSid);
        int dstAfter = countBySid(dstInv, g_plan.itemSid);
        int outHaul = haulBefore - haulAfter;
        int inDst = dstAfter - dstBefore;
        if (outHaul != inDst) {
            std::ostringstream s;
            s << "HAUL CONSERVACAO VIOLADA na entrega: carregador " << haulBefore
              << "->" << haulAfter << " (saiu " << outHaul << ") vs deposito "
              << dstBefore << "->" << dstAfter << " (entrou " << inDst
              << "). CARREGADOR DESATIVADO nesta sessao (degraded-safe).";
            diag::error(s.str());
            g_res.releaseOwner(g_plan.owner);
            g_plan = HaulPlan();
            g_disabled = true;
            return;
        }
        // Terminal: libera reservas; cooldown curto p/ o estoque assentar.
        g_res.releaseOwner(g_plan.owner);
        setCooldown(taskKeyOf(g_plan.demandUid, g_plan.itemSid),
                    HAUL_DONE_COOLDOWN);
        std::ostringstream s;
        if (moved > 0) {
            s << "HAUL #" << g_seq << " COMPLETO: " << inDst << "x \""
              << g_plan.itemName << "\" entregue em \"" << g_plan.dstName
              << "\" (deposito " << dstBefore << "->" << dstAfter
              << "; conservacao provada nas duas pontas) -- CONFIRM-HAUL-2 "
              << "(POC-005: transporte A->B executado). Demanda de origem: \""
              << g_plan.demandName << "\" -- observar a estacao voltar a "
              << "produzir (CONFIRM-HAUL-3).";
            if (haulAfter > 0) {
                s << " | " << haulAfter << " unidade(s) FICARAM com o "
                  << "carregador (deposito encheu no meio).";
            }
            diag::milestone(s.str());
        } else {
            s << "HAUL #" << g_seq << " FALHA na entrega: deposito \""
              << g_plan.dstName << "\" recusou tudo (encheu no caminho?); a "
              << "carga (" << haulAfter << ") fica com o carregador \""
              << g_plan.haulerName << "\" -- nada se perde; cooldown e "
              << "re-derivacao.";
            diag::milestone(s.str());
        }
        g_plan = HaulPlan();
        return;
    }
}

} // namespace pocs
} // namespace ls
