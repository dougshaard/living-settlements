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
#include "core/Demands.h"
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
#include <set>
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
static const float    HAUL_ARRIVE_M         = 12.0f; // chegada = a menos de 12m do CENTRO
                                                    // do predio. Evidencia 27/07: fazenda
                                                    // grande deixou o carregador a 6.4m e
                                                    // o limiar de 6m nao disparou a coleta
                                                    // (dist2=41.8 > 36) -- o char para na
                                                    // BORDA, o centro fica longe.
// Timeout por perna e por PROGRESSO, nao por tempo fixo: bases grandes tem
// caminhadas de >1500m (evidencia 17/07: haul abortava a meio caminho com o
// carregador ainda andando). So aborta se PARAR de se aproximar (pathing
// travado) por N rodadas; um teto absoluto generoso e o ultimo backstop.
static const unsigned long HAUL_LEG_STALL_MAX = 9;   // rodadas SEM progresso -> travado
static const unsigned long HAUL_LEG_HARD_CAP  = 120; // teto absoluto por perna (~20min)
static const double        HAUL_PROGRESS_EPS  = 25.0;// dist2 tem que cair ao menos isto
static const unsigned long HAUL_REEMIT_EVERY  = 6;  // re-emitir destino a cada N rodadas
static const int           HAUL_MAX_REEMITS   = 6;
static const unsigned long HAUL_FAIL_COOLDOWN = 30; // rodadas ate re-tentar a mesma tarefa
static const unsigned long HAUL_DONE_COOLDOWN = 6;  // pos-sucesso: deixa o estoque assentar
// Lease LONGO + renovado a cada rodada ativa (touch): a reserva nunca expira
// enquanto o haul avanca; expire() so recolhe orfaos que ninguem renova.
static const double        HAUL_LEASE_HOURS   = 48.0;
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
    unsigned long legStart;     // rodada em que a perna comecou (teto absoluto)
    double        legBestDist2; // menor dist2 ja visto nesta perna (-1 = novo)
    unsigned long legStall;     // rodadas consecutivas SEM aproximar
    int         reEmits;
    bool        topup;          // viagem de REPOSICAO (banda), nao de falta critica
    unsigned long seq;          // numero da viagem (logs; multi-viagem)
    std::string owner;          // dono logico das reservas ("haul:<seq>")
    HaulPlan() : active(false), phase(HP_GO_SRC),
                 srcX(0), srcY(0), srcZ(0), dstX(0), dstY(0), dstZ(0),
                 batch(0), pickedUp(0), legStart(0), legBestDist2(-1.0),
                 legStall(0), reEmits(0), topup(false), seq(0) {}
};

// MULTI-VIAGEM: ate N hauls em paralelo, um carregador por viagem. As
// reservas (por dono "haul:<seq>") ja isolam fonte/destino/carregador
// entre planos; o planejador pula demandas e carregadores ja tomados.
static const int HAUL_MAX_ACTIVE = 3;
HaulPlan      g_plans[HAUL_MAX_ACTIVE];
// Quadro de demandas da rodada (espelho p/ a aba Demandas da GUI).
std::vector<std::string> g_demandLines;
static const int HAUL_BOARD_MAX = 12;
unsigned long g_round = 0;
unsigned long g_lastLog = 0;
unsigned long g_seq = 0;
bool          g_disabled = false;   // conservacao violada -> feicao morre na sessao
double        g_lastNow = -1.0;     // deteccao de rollback (relogio andou p/ tras)
ls::domain::ReservationManager g_res;
std::map<std::string, unsigned long> g_cooldown; // taskKey -> rodada liberada
// Postos cuja chave/posicao NAO casou com predio algum deste mundo (recalculado
// pela saude das declaracoes a cada rodada): alem do AVISO no quadro, o
// idle-return NAO manda ninguem esperar em terreno vazio.
std::set<std::string> g_orphanPosts;

bool eligible(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

int activePlanCount() {
    int n = 0;
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_plans[i].active) {
            ++n;
        }
    }
    return n;
}

std::string taskKeyOf(const std::string& demandUid, const std::string& sid);

// O char e o carregador de alguma viagem ativa? (nunca redespachar/mexer)
bool porterOnTrip(Character* c) {
    if (c == 0) {
        return false;
    }
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_plans[i].active && g_plans[i].haulerHand.isValid()
            && g_plans[i].haulerHand.getCharacter() == c) {
            return true;
        }
    }
    return false;
}

// Ja existe viagem ativa para esta (estacao,item)? (dedup entre planos)
bool demandActive(const std::string& taskKey) {
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_plans[i].active
            && taskKeyOf(g_plans[i].demandUid, g_plans[i].itemSid) == taskKey) {
            return true;
        }
    }
    return false;
}

void pushDemandLine(const std::string& s) {
    if (static_cast<int>(g_demandLines.size()) < HAUL_BOARD_MAX) {
        g_demandLines.push_back(s);
    }
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

double dist2(const Ogre::Vector3& a, const Ogre::Vector3& b);

// DEDICAR CARREGADORES (fix 17/07 "todos ocupados demais"): declarar
// carregador tirava-o dos cargos NOVOS (orquestrador/guarnicao/medico) mas
// os cargos ANTIGOS da campanha ficavam -> o carregador seguia operando
// maquina, nunca ociava, nunca ia ao posto. Um carregador e papel DEDICADO:
// aqui limpamos os permajobs dele (atras da cerca, capado). Reversivel: ao
// des-declarar, o orquestrador pode reempregar. Nunca mexe no carregador da
// viagem ativa. Idempotente (nada a fazer quando ja limpo).
void dedicatePorters(GameWorld* world, PlayerInterface* pl,
                     core::CoordMode mode, const core::WriteFence& fence) {
    static const int DED_MAX_EMIT  = 8;  // remocoes por rodada (todos os carreg.)
    static const int DED_MAX_SLOTS = 64; // guarda por char
    lektor<Character*>& chars = pl->playerCharacters;
    uint32_t n = chars.size();
    if (n > HAUL_MAX_CHARS) {
        n = HAUL_MAX_CHARS;
    }
    int emitted = 0;
    for (uint32_t i = 0; i < n && emitted < DED_MAX_EMIT; ++i) {
        Character* c = chars[i];
        if (c == 0 || !core::isPorter(c) || !eligible(c)) {
            continue;
        }
        if (porterOnTrip(c)) {
            continue; // nao mexer em quem esta em viagem
        }
        if (c->getPermajobCount() <= 0) {
            continue; // ja dedicado (sem cargos)
        }
        int removed = 0, guard = 0;
        while (c->getPermajobCount() > 0 && guard < DED_MAX_SLOTS
               && emitted < DED_MAX_EMIT) {
            if (adapters::emitRemovePermajob(mode, fence, c, 0)
                    != adapters::EMIT_OK) {
                break;
            }
            ++removed; ++emitted; ++guard;
        }
        if (removed > 0) {
            std::ostringstream s;
            s << "CARREGADOR dedicado: \"" << c->getName() << "\" liberou "
              << removed << " cargo(s) de producao -- agora e so transporte "
              << "(volta ao posto quando ocioso).";
            diag::milestone(s.str());
        }
    }
}

// IDLE-RETURN (decisao do dono 17/07): carregador ocioso volta ao POSTO
// atribuido e espera -> fica pre-posicionado (e "livre mais perto da fonte"
// ja escolhe o posto certo). So mexe em quem esta OCIOSO e LONGE do posto;
// nunca no carregador da viagem ativa, nem sob ordem sua. Cap por rodada.
void returnIdlePortersToPosts(GameWorld* world, PlayerInterface* pl,
                              core::CoordMode mode, const core::WriteFence& fence) {
    static const int  RET_MAX_EMIT = 3;
    static const float RET_AT_POST_M = 8.0f; // ja no posto: nao reenviar
    lektor<Character*>& chars = pl->playerCharacters;
    uint32_t n = chars.size();
    if (n > HAUL_MAX_CHARS) {
        n = HAUL_MAX_CHARS;
    }
    int emitted = 0;
    for (uint32_t i = 0; i < n && emitted < RET_MAX_EMIT; ++i) {
        Character* c = chars[i];
        if (c == 0 || !core::isPorter(c) || !eligible(c)) {
            continue;
        }
        // Nunca um carregador em viagem ativa.
        if (porterOnTrip(c)) {
            continue;
        }
        // Autoridade do jogador e sagrada.
        hand sel = pl->selectedCharacter;
        if (sel.isValid() && sel.getCharacter() == c) {
            continue;
        }
        CharBody* body = c->getBody();
        if (body == 0 || !body->isIdle()) {
            continue; // so quem esta parado (quem ja anda p/ casa nao e ocioso)
        }
        Tasker* action = body->getCurrentAction();
        if (action != 0
            && static_cast<int>(action->priority) >= static_cast<int>(TP_OBEDIENCE)) {
            continue;
        }
        float px, py, pz;
        if (!core::porterPostPos(hand(c), px, py, pz)) {
            continue; // sem posto atribuido: fica onde esta
        }
        if (g_orphanPosts.count(core::porterPostKey(hand(c))) != 0) {
            continue; // posto nao existe NESTE mundo: avisado no quadro;
                      // ninguem espera em terreno vazio (dir.20)
        }
        Ogre::Vector3 post(px, py, pz);
        if (dist2(c->getPosition(), post)
                <= static_cast<double>(RET_AT_POST_M) * RET_AT_POST_M) {
            continue; // ja no posto
        }
        if (adapters::emitPreposition(mode, fence, c, post) == adapters::EMIT_OK) {
            ++emitted;
        }
    }
    if (emitted > 0) {
        std::ostringstream s;
        s << "POSTO: " << emitted << " carregador(es) ocioso(s) enviado(s) de "
          << "volta ao posto (pre-posicionamento).";
        diag::milestone(s.str());
    }
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
void abortHaul(HaulPlan& g_plan, const std::string& reason,
               const std::string& cooldownKey) {
    if (!g_plan.owner.empty()) {
        g_res.releaseOwner(g_plan.owner);
    }
    if (!cooldownKey.empty()) {
        setCooldown(cooldownKey, HAUL_FAIL_COOLDOWN);
    }
    std::ostringstream s;
    s << "HAUL #" << g_plan.seq << " ABORTADO (" << reason << "): \"" << g_plan.itemName
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

// DESLIGAMENTO da feicao na sessao (conservacao violada): libera TODAS as
// viagens e reservas ANTES de morrer -- com multi-viagem, desativar liberando
// so o plano violador prenderia leases e carregadores dos outros planos para
// sempre (achado de revisao 27/07; PRINC-005: falha nunca deixa reserva presa).
void disableHauling() {
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_plans[i].active) {
            if (!g_plans[i].owner.empty()) {
                g_res.releaseOwner(g_plans[i].owner);
            }
            if (g_plans[i].pickedUp > 0) {
                diag::milestone("HAUL: viagem #? encerrada pelo desligamento; a "
                                "carga fica com \"" + g_plans[i].haulerName
                                + "\" (dentro de inventario; nada se perde).");
            }
            g_plans[i] = HaulPlan();
        }
    }
    g_res.clear();
    g_disabled = true;
}

// Reset frio total (load/rollback/lifecycle): re-derivar do zero e o
// contrato (diretriz 15). Intencao nao sobrevive ao mundo mudar por fora.
void coldReset(const char* why) {
    if (activePlanCount() > 0 || g_res.leaseCount() > 0) {
        std::ostringstream s;
        s << "HAUL: reset frio (" << why << ") -- planos e reservas descartados; "
          << "re-derivacao na proxima rodada.";
        diag::milestone(s.str());
    }
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        g_plans[i] = HaulPlan();
    }
    g_res.clear();
    g_cooldown.clear();
    g_orphanPosts.clear(); // mundo novo: re-avaliar a saude das declaracoes
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
    bool        topup;    // reposicao (banda), nao falta critica
    DemandCand() : sx(0), sy(0), sz(0), d2Center(0.0), topup(false) {}
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
    uint32_t nr = rid.size();
    if (nr > 64) {
        nr = 64; // cap duro (laco nativo)
    }
    for (uint32_t i = 0; i < nr; ++i) {
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

// Coleta demandas de UM tipo (critica ou topup) na varredura ja feita.
// Pula pares em cooldown e pares com viagem ATIVA (dedup multi-viagem).
void collectDemands(lektor<RootObject*>& results, TownBase* town,
                    const Ogre::Vector3& center, bool topup,
                    std::vector<DemandCand>& demands) {
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
        if (topup) {
            pb->getResourcesNeededBecauseNotFull(need);
        } else {
            pb->getResourcesNeededBecauseEmpty(need);
        }
        uint32_t nn = need.size();
        if (nn > 64) {
            nn = 64; // cap duro no INDICE (filtrados nao contam em demands.size)
        }
        for (uint32_t k = 0; k < nn
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
            dc.topup = topup;
            std::string key = taskKeyOf(dc.stationUid, dc.itemSid);
            if (!dc.stationUid.empty() && !dc.itemSid.empty()
                && !inCooldown(key) && !demandActive(key)) {
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
}

bool planHaul(GameWorld* world, PlayerInterface* pl, TownBase* town,
              core::CoordMode mode, const core::WriteFence& fence,
              double now, bool throttle, HaulPlan& g_plan, bool allowTopup) {
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

    // 1) DEMANDAS: falta CRITICA primeiro (pull REQ-LOG-001); sem nenhuma
    // critica pendente, REPOSICAO por banda (topup) -- 1 viagem de topup
    // por vez (allowTopup), critico sempre fura a fila.
    std::vector<DemandCand> demands;
    collectDemands(results, town, center, false, demands);
    if (demands.empty() && allowTopup) {
        collectDemands(results, town, center, true, demands);
    }

    if (demands.empty()) {
        if (results.stuff != 0) {
            free(results.stuff);
            results.stuff = 0; results.count = 0; results.maxSize = 0;
        }
        if (throttle) {
            diag::log("HAUL: nenhuma demanda fora de cooldown -- nada a "
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
            // CONFIRM-HAUL-4 / JANELA DE DEMANDAS (diretriz 13): dizer o que
            // RESOLVE, nao so o que falta. Vai pro log E pro quadro da GUI.
            std::ostringstream s;
            s << "HAUL DEMANDA: \"" << dc.itemName << "\" em falta "
              << (dc.topup ? "de reposicao" : "CRITICA") << " na "
              << "estacao \"" << dc.stationName << "\" (" << dc.stationUid << ") e ";
            if (unitsInStorages > 0) {
                s << unitsInStorages << " unidade(s) ja estao em deposito -- o "
                  << "operador nativo alcanca; transporte nao resolve (v1 nao "
                  << "faz deposito->deposito). Se ele nao buscar, e outra lacuna.";
                pushDemandLine("EM DEPOSITO: " + dc.itemName + " ("
                               + dc.stationName + " deve buscar)");
            } else {
                s << "NAO ha fonte transportavel na base -- e preciso PRODUZIR/"
                  << "minerar \"" << dc.itemName << "\" (janela de demandas).";
                pushDemandLine("PRODUZIR: " + dc.itemName + " (falta p/ "
                               + dc.stationName + ")");
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
              << "demandas).";
            diag::milestone(s.str());
            pushDemandLine("FALTA ARMAZEM: " + dc.itemName + " ("
                           + src.name + " tem, nao ha onde guardar)");
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
                if (!porterAvailable(pl, c) || porterOnTrip(c)) {
                    continue; // em viagem = tomado (multi-viagem)
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
            pushDemandLine(core::porterCount() == 0
                ? std::string("DECLARE CARREGADORES (aba do painel) p/ ")
                    + dc.itemName
                : std::string("SEM CARREGADOR livre p/ ") + dc.itemName
                    + " -> " + dc.stationName);
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
        // capacidade do destino + o proprio carregador, tudo-ou-nada. O lote
        // sai do DISPONIVEL (fisico - reservado por outras viagens ativas).
        ++g_seq;
        std::ostringstream ow;
        ow << "haul:" << g_seq;
        std::string owner = ow.str();
        int batch = 0;
        {
            std::string itemRes = "item:" + dc.itemSid + "@" + src.uid;
            std::string capRes = "cap:" + dst.uid;
            // Chave do carregador pela IDENTIDADE (hand::toString; ADR-015):
            // nome duplica em roster grande ("Hive 23" em dobro) e travaria o
            // gemeo livre enquanto o xara viaja (achado de revisao 27/07).
            std::string wrkRes = "worker:" + hand(hauler).toString();
            g_res.setPhysical(itemRes, src.count);
            g_res.setPhysical(capRes, 1);
            g_res.setPhysical(wrkRes, 1);
            int avail = g_res.available(itemRes);
            batch = avail < HAUL_BATCH_MAX ? avail : HAUL_BATCH_MAX;
            if (batch <= 0) {
                --g_seq; // numero de viagem nao gasto em tentativa negada
                setCooldown(taskKeyOf(dc.stationUid, dc.itemSid), HAUL_DONE_COOLDOWN);
                continue; // tudo desta fonte ja reservado por outra viagem
            }
            std::vector<ls::domain::ReservationRequest> reqs;
            double expiry = now + HAUL_LEASE_HOURS;
            reqs.push_back(ls::domain::ReservationRequest(itemRes, owner, batch, expiry));
            reqs.push_back(ls::domain::ReservationRequest(capRes, owner, 1, expiry));
            reqs.push_back(ls::domain::ReservationRequest(wrkRes, owner, 1, expiry));
            if (!g_res.acquireAtomic(reqs, now)) {
                --g_seq; // idem: negada nao gasta numero nem merece linha por
                         // linha (dezenas de demandas disputam o mesmo tanque)
                if (throttle) {
                    diag::log("HAUL: reserva(s) negada(s) nesta rodada (recursos "
                              "ja tomados por viagens ativas) -- normal com "
                              "multi-viagem; seguindo as proximas demandas.");
                    g_lastLog = g_round;
                }
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
        g_plan.legBestDist2 = -1.0;
        g_plan.legStall = 0;
        g_plan.reEmits = 0;
        g_plan.topup = dc.topup;
        g_plan.seq = g_seq;
        g_plan.owner = owner;

        std::ostringstream s;
        s << "HAUL #" << g_seq << (dc.topup ? " (reposicao)" : "")
          << " PLANEJADO: " << batch << "x \"" << dc.itemName
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

// ---- SAUDE DAS DECLARACOES (diretriz 20): o mod LEMBRA o que o jogador
// declarou, mas o mundo carregado pode nao conter aquilo (dono nao salva;
// predio do posto pode nem existir neste save). Quem detecta a divergencia
// AVISA no quadro de demandas -- dizendo o que RESOLVE -- em vez de deixar
// "as coisas se comportarem de forma estranha" (dono, 27/07). ----
void pushDeclarationHealth(GameWorld* world, TownBase* town) {
    // Carregadores declarados que nao existem neste mundo (pendencias).
    int pend = core::pendingPorterCount();
    if (pend > 0) {
        std::vector<std::string> names;
        core::pendingPorterNames(names, 3);
        std::ostringstream s;
        s << "AVISO: " << pend << " carregador(es) declarado(s) NAO existem "
          << "neste mundo (";
        for (size_t i = 0; i < names.size(); ++i) {
            s << (i ? ", " : "") << names[i];
        }
        if (pend > static_cast<int>(names.size())) {
            s << ", +" << (pend - static_cast<int>(names.size()));
        }
        s << ") -- aguardando existirem; ou re-declare na aba.";
        pushDemandLine(s.str());
    }
    // Postos orfaos: nenhum predio deste mundo casa com a chave/posicao.
    const std::vector<core::PostEntry>& ps = core::posts();
    if (ps.empty()) {
        return;
    }
    Ogre::Vector3 center = town->getPosition();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    lektor<RootObject*> results;
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);
    size_t np = ps.size();
    if (np > 16) {
        np = 16; // cap (postos sao poucos por natureza)
    }
    g_orphanPosts.clear();
    for (size_t pi = 0; pi < np; ++pi) {
        bool found = false;
        Ogre::Vector3 ppos(ps[pi].x, ps[pi].y, ps[pi].z);
        for (uint32_t i = 0; i < results.size() && !found; ++i) {
            RootObject* o = results[i];
            if (o == 0) {
                continue;
            }
            Building* b = static_cast<Building*>(o);
            // Casa por uid (chave) OU por posicao (~6m; predio nao anda).
            if (uidOf(b) == ps[pi].key
                || dist2(b->getPosition(), ppos) <= 36.0) {
                found = true;
            }
        }
        if (!found) {
            g_orphanPosts.insert(ps[pi].key);
            pushDemandLine("AVISO: posto \"" + ps[pi].name + "\" NAO existe "
                           "neste mundo -- remova/re-declare (clique no predio); "
                           "ninguem sera enviado para la.");
        }
    }
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
}

// ---- Processa UMA viagem ativa (multi-viagem: chamada por plano/rodada).
// Mesmo corpo provado do fluxo single-haul; o parametro sombreia o antigo
// global de proposito (zero delta no codigo do caminho critico). ----
void processPlan(GameWorld* world, PlayerInterface* pl, TownBase* town,
                 core::CoordMode mode, const core::WriteFence& fence,
                 bool throttle, HaulPlan& g_plan) {
    // Re-resolver o carregador pelo HAND (identidade exata; ponteiro
    // tick-scoped, nunca guardado).
    Character* w = 0;
    if (g_plan.haulerHand.isValid()) {
        w = g_plan.haulerHand.getCharacter();
    }
    if (w == 0 || !eligible(w)) {
        abortHaul(g_plan, "carregador indisponivel (sumiu/KO/morto)",
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
        // PROGRESSO: aproximou desde o melhor ja visto? zera o contador de
        // travamento; senao, conta. So aborta se TRAVOU (pathing sem saida),
        // nunca por caminhada longa.
        if (g_plan.legBestDist2 < 0.0 || d2 < g_plan.legBestDist2 - HAUL_PROGRESS_EPS) {
            g_plan.legBestDist2 = d2;
            g_plan.legStall = 0;
        } else {
            ++g_plan.legStall;
        }
        if (g_plan.legStall > HAUL_LEG_STALL_MAX) {
            abortHaul(g_plan, "carregador travado (sem se aproximar; pathing sem rota)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        if (onLeg > HAUL_LEG_HARD_CAP) {
            abortHaul(g_plan, "teto absoluto da perna (caminhada longa demais)",
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
                s << "HAUL #" << g_plan.seq << ": destino re-emitido ("
                  << g_plan.reEmits << "/" << HAUL_MAX_REEMITS << ")";
                diag::log(s.str());
            }
        } else if (throttle) {
            std::ostringstream s;
            s << "HAUL #" << g_plan.seq << " ("
              << (g_plan.phase == HP_GO_SRC ? "rumo a fonte" : "rumo ao deposito")
              << "): dist2=" << d2 << " rodada-da-perna=" << onLeg
              << " travado=" << g_plan.legStall;
            diag::log(s.str());
            g_lastLog = g_round;
        }
        return;
    }

    // ---- Chegou. Transferencia scriptada NA MESMA rodada (cerca ja aberta). ----
    if (g_plan.phase == HP_GO_SRC) {
        Building* src = buildingByUid(world, town, g_plan.srcUid);
        if (src == 0 || src->getUseableStuff() == 0) {
            abortHaul(g_plan, "fonte nao re-resolvida (mundo mudou)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        Inventory* srcInv = src->getUseableStuff()->getInventory();
        Inventory* haulInv = w->getInventory();
        if (srcInv == 0 || haulInv == 0) {
            abortHaul(g_plan, "inventario ausente na coleta",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        int srcBefore = countBySid(srcInv, g_plan.itemSid);
        int haulBefore = countBySid(haulInv, g_plan.itemSid);
        if (srcBefore <= 0) {
            abortHaul(g_plan, "fonte esvaziou antes da coleta (corrida)",
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
              << "TODAS as viagens e reservas liberadas. Investigar antes de religar.";
            diag::error(s.str());
            disableHauling(); // libera TODOS os planos (nao so este)
            return;
        }
        if (moved <= 0 || inHaul <= 0) {
            abortHaul(g_plan, "coleta nao moveu nada (sem folga de peso/stack)",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        g_plan.pickedUp = inHaul;
        g_plan.phase = HP_GO_DST;
        g_plan.legStart = g_round;
        g_plan.legBestDist2 = -1.0;
        g_plan.legStall = 0;
        g_plan.reEmits = 0;
        // A carga agora vive no inventario do carregador: solta a reserva do
        // ITEM na fonte (cap+worker ficam) -- senao um 2o plano veria a fonte
        // falsamente esgotada durante toda a perna de entrega (revisao 27/07).
        g_res.release("item:" + g_plan.itemSid + "@" + g_plan.srcUid, g_plan.owner);
        adapters::EmitResult r = adapters::emitPreposition(
            mode, fence, w, Ogre::Vector3(g_plan.dstX, g_plan.dstY, g_plan.dstZ));
        std::ostringstream s;
        s << "HAUL #" << g_plan.seq << " COLETA OK: " << inHaul << "x \""
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
            abortHaul(g_plan, "destino nao re-resolvido (mundo mudou); carga fica "
                      "com o carregador",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        Inventory* dstInv = dst->getUseableStuff()->getInventory();
        Inventory* haulInv = w->getInventory();
        if (dstInv == 0 || haulInv == 0) {
            abortHaul(g_plan, "inventario ausente na entrega",
                      taskKeyOf(g_plan.demandUid, g_plan.itemSid));
            return;
        }
        int haulBefore = countBySid(haulInv, g_plan.itemSid);
        int dstBefore = countBySid(dstInv, g_plan.itemSid);
        if (haulBefore <= 0) {
            abortHaul(g_plan, "carregador chegou sem a carga (corrida)",
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
              << "). CARREGADOR DESATIVADO nesta sessao (degraded-safe); "
              << "TODAS as viagens e reservas liberadas.";
            diag::error(s.str());
            disableHauling(); // libera TODOS os planos (nao so este)
            return;
        }
        // Terminal: libera reservas; cooldown curto p/ o estoque assentar.
        g_res.releaseOwner(g_plan.owner);
        setCooldown(taskKeyOf(g_plan.demandUid, g_plan.itemSid),
                    HAUL_DONE_COOLDOWN);
        std::ostringstream s;
        if (moved > 0) {
            s << "HAUL #" << g_plan.seq << " COMPLETO: " << inDst << "x \""
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
            s << "HAUL #" << g_plan.seq << " FALHA na entrega: deposito \""
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

} // namespace

void poc029CarregadorTick(GameWorld* world) {
    if (world == 0 || g_disabled) {
        return;
    }
    const core::PocEnvState& env = core::pocEnv();
    // Viagem ATIVA sempre e processada (nunca deixar reserva/carga pendurada);
    // ocioso, so age com o laco ligado ou 1 ciclo pedido no painel.
    if (activePlanCount() == 0 && !env.haul && !env.haulOnce) {
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
    // RENOVA as reservas das viagens vivas ANTES de expirar orfaos: enquanto
    // a tarefa avanca, a reserva nunca vence (caminhada longa nao mata haul).
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_plans[i].active) {
            g_res.touch(g_plans[i].owner, now + HAUL_LEASE_HOURS);
        }
    }
    g_res.expire(now);
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_plans[i].active && !g_res.ownerHasLeases(g_plans[i].owner)) {
            abortHaul(g_plans[i], "reserva perdida (recurso sumiu por fora)",
                      taskKeyOf(g_plans[i].demandUid, g_plans[i].itemSid));
        }
    }

    // Dedicar carregadores (liberar cargos de producao antigos) e mandar os
    // ociosos ao posto -- so com transporte auto ligado. Dedicar roda mesmo
    // sem posto declarado (carregador dedicado deve ficar livre de qualquer
    // forma); voltar-ao-posto exige um posto.
    if (env.haul && core::porterCount() > 0) {
        dedicatePorters(world, pl, mode, fence);
    }
    if (env.haul && core::postCount() > 0) {
        returnIdlePortersToPosts(world, pl, mode, fence);
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

    // ---- Quadro de demandas desta rodada (a aba da GUI le o espelho) ----
    g_demandLines.clear();
    // Saude das declaracoes primeiro (dir.20): divergencia mod-vs-mundo e o
    // aviso mais importante do quadro -- explica qualquer estranheza antes
    // que o jogador precise adivinhar.
    pushDeclarationHealth(world, town);

    // ---- Processa TODAS as viagens ativas (multi-viagem) ----
    for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
        if (g_disabled) {
            break; // conservacao violou no meio do laco: nada mais roda
        }
        if (g_plans[i].active) {
            processPlan(world, pl, town, mode, fence, throttle, g_plans[i]);
        }
        if (g_plans[i].active) { // ainda ativa apos processar -> status no quadro
            std::ostringstream s;
            s << "EM TRANSPORTE: " << g_plans[i].batch << "x "
              << g_plans[i].itemName
              << (g_plans[i].topup ? " (reposicao)" : "")
              << " -> " << g_plans[i].dstName << " [" << g_plans[i].haulerName
              << ", " << (g_plans[i].phase == HP_GO_SRC ? "indo a fonte"
                                                        : "levando ao deposito")
              << "]";
            pushDemandLine(s.str());
        }
    }

    // ---- Planeja ate UMA viagem nova por rodada (suave). NUNCA com a
    // feicao desativada nesta mesma rodada (emitiria viagem orfa que
    // nenhum tick futuro processaria -- achado de revisao 27/07). ----
    int active = activePlanCount();
    if (!g_disabled && active < HAUL_MAX_ACTIVE && (env.haul || env.haulOnce)) {
        bool topupActive = false;
        int slot = -1;
        for (int i = 0; i < HAUL_MAX_ACTIVE; ++i) {
            if (g_plans[i].active && g_plans[i].topup) {
                topupActive = true;
            }
            if (!g_plans[i].active && slot < 0) {
                slot = i;
            }
        }
        if (slot >= 0) {
            bool planned = planHaul(world, pl, town, mode, fence, now, throttle,
                                    g_plans[slot], !topupActive);
            if (planned && g_plans[slot].active) {
                // Quadro reflete a viagem JA nesta rodada (senao a GUI diria
                // "logistica em dia" com carregador recem-despachado).
                std::ostringstream s;
                s << "EM TRANSPORTE: " << g_plans[slot].batch << "x "
                  << g_plans[slot].itemName
                  << (g_plans[slot].topup ? " (reposicao)" : "")
                  << " -> " << g_plans[slot].dstName << " ["
                  << g_plans[slot].haulerName << ", indo a fonte]";
                pushDemandLine(s.str());
            }
            if (env.haulOnce) {
                core::pocEnvMutable().haulOnce = false; // pedido do painel atendido
                if (!planned) {
                    diag::milestone("HAUL (1 ciclo): nenhuma viagem planejada nesta "
                                    "rodada -- ver linhas HAUL acima pelo motivo.");
                }
            }
        }
    }

    if (g_demandLines.empty()) {
        pushDemandLine(env.haul ? "(sem demandas pendentes; logistica em dia)"
                                : "(transporte auto desligado)");
    }
    core::demandsSet(g_demandLines);
}

} // namespace pocs
} // namespace ls
