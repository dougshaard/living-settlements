// Living Settlements -- domain/WorkModel.h
// -----------------------------------------------------------------
// Modelo de dados PURO do nucleo de coordenacao de trabalho.
// Fonte: docs/design/nucleo-de-trabalho.md, secoes 3.2 (snapshot POD)
// e 3.3 (estruturas de dominio).
//
// Fronteira de pureza (ADR-014; piramide de testes 16.1 nivel 1): SEM
// KenshiLib / Ogre / Boost. O SnapshotBuilder (adapter) traduz
// ponteiros/enums nativos para estes PODs, keyados por ID estavel; o
// dominio NUNCA ve ponteiros do jogo nem enums nativos (REQ-PER-001).
// Secao 2.5 / invariante 12: nunca hardcodar ordinal nativo -- o mapa
// TaskType-nativo <-> WorkVerb vive so no adapter, por constante nomeada.
//
// C++03-conservador (RISK-009, toolset v100). ASCII-only (sem BOM).
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_WORKMODEL_H
#define LS_DOMAIN_WORKMODEL_H

#include <string>
#include <vector>

namespace ls {
namespace domain {

typedef std::string WorkerId;   // hand serializado / getName (nunca indice de roster)
typedef std::string StationId;  // Building::InstanceID.uid como string
typedef double      Tick;       // horas-de-jogo (getTimeStamp_inGameHours().getTotalHours())

// Verbos de trabalho que o coordenador raciocina. Mirror NEUTRO: o
// adapter mapeia TaskType nativo (constante nomeada) <-> WorkVerb.
// Nunca usar o inteiro nativo aqui (secao 2.5 / invariante 12).
enum WorkVerb {
    WV_UNKNOWN = 0,
    WV_OPERATE_MACHINERY,
    WV_DELIVER_RESOURCES,
    WV_OPERATE_STORAGE,
    WV_REPAIR,
    WV_BUILD
};

// Estado de producao (mirror de ProductionBuilding::productionState).
enum ProdState {
    PROD_UNKNOWN = 0,
    PROD_NORMAL,
    PROD_STARVED,     // falta insumo (montante) -> lane LOGISTICS
    PROD_FULL,        // saida entupida (jusante) -> lane LOGISTICS
    PROD_IMPOSSIBLE   // cadeia quebrada (sem recurso/energia/config)
};

// Banda de fome (mirror dos limiares nativos calibrados; secao 6).
enum HungerBand {
    HUNGER_OK = 0,
    HUNGER_REALLY_HUNGRY,     // ~ ponto de abandono de posto [H8]
    HUNGER_KO,
    HUNGER_POINT_OF_NO_RETURN
};

// Classe de trabalho de um posto (mirror neutro; o adapter mapeia de
// BuildingFunction por constante nomeada). So PRODUCAO recebe operador
// no nucleo inicial -- treino/gaiola/etc. nao sao "manter a base viva"
// (achado empirico do 1o run do Marco 0: a lane OPERATOR estava propondo
// bonecos de treino).
enum WorkClass {
    WC_OTHER = 0,     // storage/cama/gaiola/parede/... nao despachavel
    WC_PRODUCTION,    // mina/refinaria/crafting/fornalha/gerador/pesquisa
    WC_TRAINING       // treino (adiado; lane propria futura)
};

// Distancia ao quadrado entre dois pontos (evita sqrt; usada so p/ ordenar
// e para o filtro de proximidade).
double distanceSquared(double ax, double ay, double az,
                       double bx, double by, double bz);

// Gate mestre de incapacitacao (flags primeiro, NUNCA a acao GOAP).
// proneState mantido como int neutro (adapter mapeia getProneState()).
struct MedicalGates {
    bool isDead;
    bool isUnconcious;
    int  proneState;   // 0 = normal; != 0 = anormal (aleijado/fingindo/KO/levantando)
    MedicalGates() : isDead(false), isUnconcious(false), proneState(0) {}
    // Incapaz de receber trabalho agora (gate composto de flags).
    bool incapacitated() const {
        return isDead || isUnconcious || proneState != 0;
    }
};

// Cargo nativo (permajob). roleMachineId = subject [H3]; vazio quando o
// cargo nao tem alvo fixo (fallback: pertenca em currentOperators).
struct PermajobView {
    int         verb;          // WorkVerb
    StationId   roleMachineId; // subject.uid; "" se ausente [H3]
    std::string roleName;      // rotulo humano (painel Por-Que)
    PermajobView() : verb(WV_UNKNOWN) {}
};

// Nivel de skill do worker para um stat. Par neutro: o adapter resolve
// getStatUsed() <-> getStat(). statId e opaco (StatsEnumerated nativo).
struct SkillLevel {
    int statId;
    int level;
    SkillLevel() : statId(-1), level(0) {}
    SkillLevel(int s, int l) : statId(s), level(l) {}
};

struct WorkerView {
    WorkerId     id;
    std::string  name;
    bool         isAnimal;        // excluido do pool (secao 4 / 7.2)
    std::vector<PermajobView> permajobs;
    std::vector<SkillLevel>   skills;   // stats relevantes (dos postos no snapshot)
    bool         isIdle;          // so acionavel se estavel por N leituras (debounce)
    MedicalGates medical;
    int          hungerBand;      // HungerBand
    bool         canTakeOrders;   // canTakePlayerOrdersAtThisTime (NUNCA sozinho)
    int          currentPriority; // banda de taskPriority (informativo/log)
    bool         underDirectOrder;// currentPriority >= TP_OBEDIENCE (adapter resolve)
    bool         selectedByPlayer;// em selectedCharacter OU selectedCharacters
    double       posX, posY, posZ;// posicao (para distancia; nunca despachar longe)

    WorkerView() : isAnimal(false), isIdle(false), hungerBand(HUNGER_OK),
                   canTakeOrders(false), currentPriority(0),
                   underDirectOrder(false), selectedByPlayer(false),
                   posX(0.0), posY(0.0), posZ(0.0) {}

    // Nivel para o stat; -1 se desconhecido no snapshot.
    int skillFor(int statId) const;
    // Algum permajob mira esta maquina (papel = permajob; secao 8.1).
    bool servesMachine(const StationId& station) const;
    // Tem algum cargo fixo (permajob)? (WorkerPool exclui "fora-de-permajob").
    bool hasPermajob() const { return !permajobs.empty(); }

    // Gate de AUTORIDADE composto (invariante 7.1.3): a autoridade do
    // jogador e sagrada. NENHUM sinal sozinho basta -- todos devem valer.
    // canTakeOrders retorna TRUE ate para KO [R], por isso a composicao.
    bool isAuthorizableTarget() const {
        return canTakeOrders
            && !underDirectOrder
            && !selectedByPlayer
            && !medical.incapacitated();
    }
};

// Necessidade de item declarada por um storage/producao (logistica pull).
struct ItemNeed {
    std::string itemKey;  // "item:<gamedata-uid>"
    int         amount;
    ItemNeed() : amount(0) {}
    ItemNeed(const std::string& k, int a) : itemKey(k), amount(a) {}
};

struct StationView {
    StationId   id;
    int         function;         // BuildingFunction (opaco)
    bool        needsOperating;
    std::vector<WorkerId> operatorsNow; // currentOperators -> ids estaveis
    int         operatorsMax;
    bool        dontNeedWork;     // dontNeedWorkRightNow() [H]
    int         productionState;  // ProdState
    bool        powerOk;          // !( isOutOfPower() > 0 )
    bool        broken;           // isBroken()
    std::vector<ItemNeed> needsCritical; // getResourcesNeededBecauseEmpty
    std::vector<ItemNeed> needsTopup;    // getResourcesNeededBecauseNotFull
    std::vector<ItemNeed> surplus;       // getItemsWeWantRidOf
    int         statUsed;         // StatsEnumerated do posto (opaco); -1 se n/a
    int         workClass;        // WorkClass (adapter mapeia de BuildingFunction)
    double      posX, posY, posZ;

    StationView() : function(0), needsOperating(false), operatorsMax(0),
                    dontNeedWork(false), productionState(PROD_UNKNOWN),
                    powerOk(true), broken(false), statUsed(-1),
                    workClass(WC_OTHER), posX(0.0), posY(0.0), posZ(0.0) {}

    bool hasFreeSlot() const {
        return operatorsMax > static_cast<int>(operatorsNow.size());
    }
};

struct WorldSnapshot {
    Tick nowHours;
    bool readGateOpen;   // false => leitura suprimida (load/reset): nada acionavel
    double baseX, baseY, baseZ; // ancora da base (TownBase::getPosition)
    double baseRadius;          // TownBase::getRadius (escala do filtro de proximidade)
    std::vector<WorkerView>  workers;
    std::vector<StationView> stations;
    WorldSnapshot() : nowHours(0.0), readGateOpen(false),
                      baseX(0.0), baseY(0.0), baseZ(0.0), baseRadius(0.0) {}
};

// ---------------------------------------------------------------
// Estruturas de decisao (secao 3.3)
// ---------------------------------------------------------------

enum Lane {
    LANE_OPERATOR = 0,  // unica lane do nucleo inicial (Marco 1-3)
    LANE_LOGISTICS,     // STARVED / getResourcesNeeded* (Marco 4+)
    LANE_POWER,         // isOutOfPower (Marco 4+)
    LANE_BUILD          // ConstructionState / isBroken (Marco 4+)
};

enum GapType {
    GAP_UNMANNED = 0,   // posto sem operador (lane OPERATOR)
    GAP_STARVED,        // lane LOGISTICS
    GAP_FULL,           // lane LOGISTICS
    GAP_IMPOSSIBLE,
    GAP_PULL_CRITICAL,  // lane LOGISTICS
    GAP_PULL_TOPUP,     // lane LOGISTICS
    GAP_REPAIR,         // lane BUILD
    GAP_BUILD           // lane BUILD
};

typedef std::string TaskKey; // canonico deterministico (secao 3.1)

// TaskKey = chave canonica de (station, verb, resource). Deterministico:
// a mesma lacuna gera a MESMA chave em todo tick -> dedup automatico
// entre ticks e entre atribuidores (pre-condicao do IntentLedger e da
// atribuicao sticky sem flip-flop).
TaskKey makeTaskKey(const StationId& station, int verb, const std::string& resource);

struct Gap {
    GapType     type;
    int         lane;
    StationId   targetPostId;
    std::string itemKey;      // opcional (lanes de logistica)
    Tick        sinceHours;   // desde quando a condicao esta estavel
    WorkerId    servedBy;     // "" se ninguem ja serve (short-circuit)
    TaskKey     key;
    int         requiredStat; // statUsed do posto; -1 se n/a
    int         verb;         // WorkVerb a emitir
    double      posX, posY, posZ; // posicao do posto (desempate por distancia)

    Gap() : type(GAP_UNMANNED), lane(LANE_OPERATOR), sinceHours(0.0),
            requiredStat(-1), verb(WV_UNKNOWN),
            posX(0.0), posY(0.0), posZ(0.0) {}
};

// Intent: memoria de escrita / verificar-antes-de-confiar (secao 3.3 / 7.4).
enum IntentState { INTENT_PENDING = 0, INTENT_CONFIRMED, INTENT_FAILED, INTENT_DONE };
enum IntentVerb  { IV_SET_DESTINATION = 0, IV_ADD_ORDER };

struct Intent {
    WorkerId    workerId;
    TaskKey     taskKey;
    StationId   targetStation;    // posto-alvo (criterio de CONFIRM; secao 7.4)
    int         verb;             // IntentVerb
    Tick        emittedHours;
    int         state;            // IntentState
    std::string reservationOwner; // "task:<TaskKey>"

    Intent() : verb(IV_ADD_ORDER), emittedHours(0.0), state(INTENT_PENDING) {}
    bool isActive() const { return state == INTENT_PENDING || state == INTENT_CONFIRMED; }
    bool isTerminal() const { return state == INTENT_FAILED || state == INTENT_DONE; }
};

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_WORKMODEL_H
