// Living Settlements -- tests/unit/test_taskboard.cpp
// Testes de TaskBoard (classificacao de lacunas + lanes + servedBy) e
// WorkerPool (filtro de despachaveis). Dominio puro.
//
//   (uma linha) g++ -std=c++03 -Wall -Wextra -I../../plugin/src
//   ../../plugin/src/domain/WorkModel.cpp
//   ../../plugin/src/domain/OperatorReconciler.cpp
//   ../../plugin/src/domain/TaskBoard.cpp
//   ../../plugin/src/domain/WorkerPool.cpp
//   test_taskboard.cpp -o test_taskboard
#include "domain/WorkModel.h"
#include "domain/TaskBoard.h"
#include "domain/WorkerPool.h"
#include "domain/OperatorReconciler.h"

#include <iostream>
#include <vector>
#include <cstddef>

using namespace ls::domain;

static int g_checks = 0;
static int g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #cond << "\n"; } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; if (!((a) == (b))) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #a " == " #b \
              << " (obtido " << (a) << " vs " << (b) << ")\n"; } } while (0)

static StationView makeStation(const StationId& id, bool needsOp, int maxOp,
                               int prodState, bool powerOk, bool broken,
                               bool dontNeedWork, int statUsed,
                               int workClass = WC_PRODUCTION) {
    StationView s;
    s.id = id; s.needsOperating = needsOp; s.operatorsMax = maxOp;
    s.productionState = prodState; s.powerOk = powerOk; s.broken = broken;
    s.dontNeedWork = dontNeedWork; s.statUsed = statUsed;
    s.workClass = workClass;
    return s;
}

static WorkerView makeIdleWorker(const WorkerId& id) {
    WorkerView w;
    w.id = id; w.name = id;
    w.isIdle = true;
    w.canTakeOrders = true;   // autorizavel por padrao; testes desligam pontualmente
    return w;
}

static int countType(const std::vector<Gap>& g, GapType t) {
    int n = 0;
    for (std::size_t i = 0; i < g.size(); ++i) if (g[i].type == t) ++n;
    return n;
}

static void test_gaps_classification() {
    WorldSnapshot snap;
    snap.readGateOpen = true;

    // s1: posto NORMAL sem operador com vaga -> UNMANNED (lane OPERATOR).
    snap.stations.push_back(makeStation("s1", true, 2, PROD_NORMAL, true, false, false, 7));
    // s2: quebrado -> REPAIR (lane BUILD), antes de qualquer coisa.
    snap.stations.push_back(makeStation("s2", true, 1, PROD_NORMAL, true, true, false, 7));
    // s3: STARVED -> lane LOGISTICS, nunca operador.
    snap.stations.push_back(makeStation("s3", true, 1, PROD_STARVED, true, false, false, 7));
    // s4: precisa operar mas ja esta CHEIO -> sem lacuna.
    StationView s4 = makeStation("s4", true, 1, PROD_NORMAL, true, false, false, 7);
    s4.operatorsNow.push_back("someone");
    snap.stations.push_back(s4);
    // s5: vaga livre mas dispensada (dontNeedWork) -> sem lacuna.
    snap.stations.push_back(makeStation("s5", true, 2, PROD_NORMAL, true, false, true, 7));
    // s6: UNMANNED, mas um permajob ja mira -> servedBy setado.
    snap.stations.push_back(makeStation("s6", true, 2, PROD_NORMAL, true, false, false, 9));
    // s7: precisa operar, livre, NORMAL, mas e TREINO -> NAO vira lacuna OPERATOR.
    snap.stations.push_back(makeStation("s7", true, 2, PROD_NORMAL, true, false, false, 9, WC_TRAINING));

    // Worker cujo permajob mira s6 (serve a maquina).
    WorkerView wperma; wperma.id = "wperma";
    PermajobView pj; pj.verb = WV_OPERATE_MACHINERY; pj.roleMachineId = "s6";
    wperma.permajobs.push_back(pj);
    snap.workers.push_back(wperma);

    std::vector<Gap> gaps;
    buildGaps(snap, gaps);

    CHECK_EQ(countType(gaps, GAP_UNMANNED), 2);   // s1 e s6
    CHECK_EQ(countType(gaps, GAP_REPAIR), 1);     // s2
    CHECK_EQ(countType(gaps, GAP_STARVED), 1);    // s3
    // s4 (cheio) e s5 (dispensado) nao geram lacuna nenhuma.
    CHECK_EQ(gaps.size(), static_cast<std::size_t>(4));

    // Acha a lacuna de s1 e valida chave/lane/stat.
    bool foundS1 = false, foundS6 = false;
    for (std::size_t i = 0; i < gaps.size(); ++i) {
        if (gaps[i].targetPostId == "s1") {
            foundS1 = true;
            CHECK_EQ(gaps[i].lane, static_cast<int>(LANE_OPERATOR));
            CHECK_EQ(gaps[i].requiredStat, 7);
            CHECK_EQ(gaps[i].verb, static_cast<int>(WV_OPERATE_MACHINERY));
            CHECK_EQ(gaps[i].key, makeTaskKey("s1", WV_OPERATE_MACHINERY, "cap:s1"));
            CHECK(gaps[i].servedBy.empty());
        }
        if (gaps[i].targetPostId == "s6") {
            foundS6 = true;
            CHECK_EQ(gaps[i].servedBy, std::string("wperma")); // short-circuit
        }
    }
    CHECK(foundS1);
    CHECK(foundS6);
    // s7 (treino) nao deve gerar lacuna alguma (refinamento pos-1o run).
    for (std::size_t i = 0; i < gaps.size(); ++i) {
        CHECK(gaps[i].targetPostId != "s7");
    }

    // operatorGaps: so OPERATOR nao-servidas -> apenas s1 (s6 esta servida).
    std::vector<Gap> ops;
    operatorGaps(gaps, ops);
    CHECK_EQ(ops.size(), static_cast<std::size_t>(1));
    if (!ops.empty()) CHECK_EQ(ops[0].targetPostId, std::string("s1"));
}

static void test_read_gate_closed() {
    WorldSnapshot snap;         // readGateOpen = false por padrao (fail-closed)
    snap.stations.push_back(makeStation("s1", true, 2, PROD_NORMAL, true, false, false, 7));
    snap.workers.push_back(makeIdleWorker("w1"));

    std::vector<Gap> gaps;
    buildGaps(snap, gaps);
    CHECK(gaps.empty());        // gate fechado => nada acionavel

    std::vector<std::size_t> pool;
    buildPool(snap, 0.0, pool);
    CHECK(pool.empty());
}

static void test_worker_pool() {
    WorldSnapshot snap;
    snap.readGateOpen = true;

    WorkerView w1 = makeIdleWorker("w1");                 // despachavel
    snap.workers.push_back(w1);

    WorkerView w2 = makeIdleWorker("w2"); w2.isAnimal = true;         // animal
    snap.workers.push_back(w2);

    WorkerView w3 = makeIdleWorker("w3");                              // tem permajob
    PermajobView pj; pj.verb = WV_OPERATE_MACHINERY; pj.roleMachineId = "sX";
    w3.permajobs.push_back(pj);
    snap.workers.push_back(w3);

    WorkerView w4 = makeIdleWorker("w4"); w4.underDirectOrder = true; // ordem direta
    snap.workers.push_back(w4);

    WorkerView w5 = makeIdleWorker("w5"); w5.selectedByPlayer = true; // selecionado
    snap.workers.push_back(w5);

    WorkerView w6 = makeIdleWorker("w6"); w6.medical.isUnconcious = true; // KO
    snap.workers.push_back(w6);

    WorkerView w7 = makeIdleWorker("w7"); w7.hungerBand = HUNGER_KO;  // KO de fome
    snap.workers.push_back(w7);

    WorkerView w8 = makeIdleWorker("w8"); w8.isIdle = false;          // ocupado
    snap.workers.push_back(w8);

    std::vector<std::size_t> pool;
    buildPool(snap, 0.0, pool);
    CHECK_EQ(pool.size(), static_cast<std::size_t>(1)); // so w1
    if (!pool.empty()) CHECK_EQ(snap.workers[pool[0]].id, std::string("w1"));

    // Predicado unitario direto.
    CHECK(isDispatchable(w1));
    CHECK(!isDispatchable(w2));
    CHECK(!isDispatchable(w3));
    CHECK(!isDispatchable(w4));
    CHECK(!isDispatchable(w5));
    CHECK(!isDispatchable(w6));
    CHECK(!isDispatchable(w7));
    CHECK(!isDispatchable(w8));
}

static void test_pool_proximity() {
    WorldSnapshot snap; snap.readGateOpen = true;
    snap.baseX = 0.0; snap.baseY = 0.0; snap.baseZ = 0.0;
    WorkerView perto = makeIdleWorker("perto"); perto.posX = 10.0;
    WorkerView longe = makeIdleWorker("longe"); longe.posX = 1000.0;
    snap.workers.push_back(perto);
    snap.workers.push_back(longe);

    std::vector<std::size_t> pool;
    buildPool(snap, 100.0, pool);                 // raio 100m: exclui o longe
    CHECK_EQ(pool.size(), static_cast<std::size_t>(1));
    if (!pool.empty()) CHECK_EQ(snap.workers[pool[0]].id, std::string("perto"));

    buildPool(snap, 0.0, pool);                   // sem filtro -> ambos
    CHECK_EQ(pool.size(), static_cast<std::size_t>(2));
}

int main() {
    test_gaps_classification();
    test_read_gate_closed();
    test_worker_pool();
    test_pool_proximity();

    std::cout << g_checks << " verificacoes, " << g_fails << " falhas\n";
    if (g_fails == 0) { std::cout << "OK\n"; return 0; }
    return 1;
}
