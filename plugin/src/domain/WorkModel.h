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
#include <map>

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
    WV_BUILD,
    // Parte 5: verbos de producao que uma estacao pode declarar via
    // getDefaultTask() (secao 9). Mapeados por constante nomeada no adapter.
    WV_FILL_MACHINE,
    WV_OPERATE_AUTOMATIC,
    WV_COLLECT_OUTPUT,
    WV_EMPTY_OUTPUTS
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
    double       carryNow;        // getTotalCarryWeight() [V]; peso carregado agora
    double       carryMax;        // getStat(_MaxCarryWeight) [V]; 0 = desconhecido
    bool         carryObserved;   // inv.21: peso derivado do inventario (mutado em
                                  // worker thread) -> so lido no ramo thread-safe

    WorkerView() : isAnimal(false), isIdle(false), hungerBand(HUNGER_OK),
                   canTakeOrders(false), currentPriority(0),
                   underDirectOrder(false), selectedByPlayer(false),
                   posX(0.0), posY(0.0), posZ(0.0), carryNow(0.0), carryMax(0.0),
                   carryObserved(false) {}

    // Fracao da capacidade carregada (inv.17). 0 se capacidade desconhecida ou
    // nao observada. So confiavel quando carryObserved (rodada thread-safe).
    double loadRatio() const {
        return (carryObserved && carryMax > 0.0) ? (carryNow / carryMax) : 0.0;
    }
    // Peso conhecido E pesado (inv.17): so exclui quem foi observado carregado.
    bool observedHeavy(double maxRatio) const {
        return carryObserved && loadRatio() >= maxRatio;
    }
    // Maos vazias (elegivel a desstaffar/reatribuir sem largar carga; inv.17).
    bool handsEmpty(double eps) const { return carryNow <= eps; }

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

// Espelho de um input ConsumptionItem [V] (StorageBuilding.h:31): quanto ha e
// capacidade. Alimenta o fill agregado por recurso (banda morta / subida
// observada, parte 5 secao 4). Amounts sao mutados em worker thread -> so
// preencher no ramo thread-safe (prodObserved).
struct StockSlotView {
    std::string itemKey;    // "item:<gamedata-stringID>"
    double      amount;
    double      maxCapacity;
    StockSlotView() : amount(0.0), maxCapacity(0.0) {}
    StockSlotView(const std::string& k, double a, double m)
        : itemKey(k), amount(a), maxCapacity(m) {}
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
    // -- Parte 5 (recursos/estoque) --------------------------------------
    int         defaultVerb;      // WorkVerb via getDefaultTask (secao 9); WV_UNKNOWN se n/a
    int         defaultTaskNative;// TaskType nativo cru (opaco; log H1-verbo); -1 se n/a
    bool        operatorsObserved;// inv.21: operatorsNow lido no ramo thread-safe
    bool        prodObserved;     // inv.21: productionState/estoque lidos thread-safe
    double      veinRichness;     // getMiningResourceLevel() [H4]; -1 se n/a
    bool        producesObserved; // producesItemKey lido com sucesso
    std::vector<ItemNeed> needsCritical; // getResourcesNeededBecauseEmpty (< MIN)
    std::vector<ItemNeed> needsTopup;    // getResourcesNeededBecauseNotFull (< ALVO)
    std::vector<ItemNeed> surplus;       // getItemsWeWantRidOf
    std::vector<StockSlotView> inputs;   // ConsumptionItem por input (amount/cap)
    std::string producesItemKey;  // "item:<gamedata-stringID>"; "" se n/a
    std::string producesItemName; // nome humano do item produzido (log)
    int         statUsed;         // StatsEnumerated do posto (opaco); -1 se n/a
    int         workClass;        // WorkClass (adapter mapeia de BuildingFunction)
    double      posX, posY, posZ;

    StationView() : function(0), needsOperating(false), operatorsMax(0),
                    dontNeedWork(false), productionState(PROD_UNKNOWN),
                    powerOk(true), broken(false), defaultVerb(WV_UNKNOWN),
                    defaultTaskNative(-1), operatorsObserved(false),
                    prodObserved(false), veinRichness(-1.0),
                    producesObserved(false), statUsed(-1),
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
    // Estoque REAL da base por item: soma de Inventory::getNumItems() sobre as
    // storages (BF_RESOURCE_STORAGE/BF_GENERAL_STORAGE). E o sinal correto p/ a
    // SUBIDA-OBSERVADA (parte 5 sec.4.4) -- o ConsumptionItem da maquina e so o
    // buffer de input (~0), NAO o deposito (achado de calibracao 2026-07-11).
    // Inventario e mutado em worker thread -> so preenchido no ramo thread-safe;
    // baseStockObserved diz se foi lido neste tick (senao: sem sinal, nao 0).
    std::map<std::string, double> baseStock;
    bool baseStockObserved;
    WorldSnapshot() : nowHours(0.0), readGateOpen(false),
                      baseX(0.0), baseY(0.0), baseZ(0.0), baseRadius(0.0),
                      baseStockObserved(false) {}
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
