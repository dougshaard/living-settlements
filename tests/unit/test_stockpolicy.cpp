// Living Settlements -- tests/unit/test_stockpolicy.cpp
// Testes do StockPolicy (parte 5, dominio puro): agregacao por recurso, banda
// morta (Empty subset NotFull) + fallback com clamp, debounce, subida-observada,
// would-staff com gates de peso (inv.17) e servedBy (F8).
//
//   (uma linha) g++ -std=c++03 -Wall -Wextra -I../../plugin/src
//   ../../plugin/src/domain/WorkModel.cpp
//   ../../plugin/src/domain/StockPolicy.cpp
//   test_stockpolicy.cpp -o test_stockpolicy
#include "domain/WorkModel.h"
#include "domain/StockPolicy.h"

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

static StationView mineOf(const StationId& id, const std::string& produces,
                          int prodState = PROD_NORMAL) {
    StationView s;
    s.id = id; s.needsOperating = true; s.operatorsMax = 1;
    s.productionState = prodState; s.powerOk = true; s.broken = false;
    s.dontNeedWork = false; s.statUsed = 7; s.workClass = WC_PRODUCTION;
    s.operatorsObserved = true; s.prodObserved = true; s.producesObserved = true;
    s.producesItemKey = produces; s.producesItemName = produces;
    s.defaultVerb = WV_OPERATE_MACHINERY;
    return s;
}

static StationView sinkOf(const StationId& id) {
    StationView s;
    s.id = id; s.workClass = WC_PRODUCTION;
    s.operatorsObserved = true; s.prodObserved = true;
    return s;
}

static WorkerView workerOf(const WorkerId& id, double carryNow, double carryMax) {
    WorkerView w;
    w.id = id; w.name = id; w.isIdle = true; w.canTakeOrders = true;
    w.carryNow = carryNow; w.carryMax = carryMax;
    w.carryObserved = true;   // simula rodada thread-safe (peso confiavel)
    return w;
}

static const ResourceVerdict* find(const StockReport& rep, const std::string& key) {
    for (std::size_t i = 0; i < rep.resources.size(); ++i) {
        if (rep.resources[i].itemKey == key) return &rep.resources[i];
    }
    return 0;
}

static void test_aggregation_and_tiers() {
    WorldSnapshot snap; snap.readGateOpen = true;
    StationView f = sinkOf("f1");
    f.needsCritical.push_back(ItemNeed("iron", 1));
    f.needsTopup.push_back(ItemNeed("iron", 1));    // Empty subset NotFull
    f.needsTopup.push_back(ItemNeed("copper", 1));
    f.inputs.push_back(StockSlotView("iron", 2.0, 10.0));
    snap.stations.push_back(f);
    snap.stations.push_back(mineOf("m1", "iron"));
    snap.stations.push_back(mineOf("m2", "iron"));

    StockPolicy sp;
    StockConfig cfg; cfg.demandConsistentTicks = 1;   // sem atraso de debounce
    std::vector<std::size_t> pool;
    StockReport rep;
    sp.evaluate(snap, pool, cfg, rep);

    const ResourceVerdict* iron = find(rep, "iron");
    CHECK(iron != 0);
    if (iron) {
        CHECK_EQ(iron->criticalSinks, 1);
        CHECK_EQ(iron->topupSinks, 1);
        CHECK_EQ(iron->producers, 2);
        CHECK_EQ(iron->tier, static_cast<int>(STK_CRITICAL));  // >=1 critico -> HUNGRY
        CHECK(iron->stable);
    }
    const ResourceVerdict* copper = find(rep, "copper");
    CHECK(copper != 0);
    if (copper) {
        CHECK_EQ(copper->criticalSinks, 0);
        CHECK_EQ(copper->topupSinks, 1);
        CHECK_EQ(copper->tier, static_cast<int>(STK_LOW));     // topup, nao critico -> BAND
    }
    CHECK_EQ(rep.hungryResources, 1);
    // Ordenacao: iron (1 critico) vem antes de copper (0 critico).
    CHECK(rep.resources.size() >= 2);
    if (rep.resources.size() >= 2) CHECK_EQ(rep.resources[0].itemKey, std::string("iron"));
}

static void test_sated_when_no_topup() {
    WorldSnapshot snap; snap.readGateOpen = true;
    // Sink sem necessidades declaradas, mas ha produtor -> SATED (topup==0).
    snap.stations.push_back(mineOf("m1", "iron"));
    StockPolicy sp; StockConfig cfg; cfg.demandConsistentTicks = 1;
    std::vector<std::size_t> pool; StockReport rep;
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* iron = find(rep, "iron");
    // Sem need-list e sem inputs (aggCap 0) -> UNKNOWN (nem HUNGRY nem SATED falso).
    CHECK(iron != 0);
    if (iron) CHECK_EQ(iron->tier, static_cast<int>(STK_UNKNOWN));
}

static void test_fill_fallback_clamp() {
    WorldSnapshot snap; snap.readGateOpen = true;
    // Sink com input quase vazio e SEM need-list -> fallback por fill -> HUNGRY.
    StationView f = sinkOf("f1");
    f.inputs.push_back(StockSlotView("water", 1.0, 100.0)); // fill 0.01 <= 0.15
    snap.stations.push_back(f);
    StockPolicy sp; StockConfig cfg; cfg.demandConsistentTicks = 1;
    std::vector<std::size_t> pool; StockReport rep;
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* w = find(rep, "water");
    CHECK(w != 0);
    if (w) {
        CHECK_EQ(w->tier, static_cast<int>(STK_CRITICAL));  // fill baixo -> HUNGRY
        CHECK(w->fillRatio < 0.02);
    }
    // Banda estreita demais e clampada: mesmo com MIN/ALVO colados, mantem gap.
    StockPolicy sp2; StockConfig narrow; narrow.demandConsistentTicks = 1;
    narrow.fillMin = 0.49; narrow.fillTarget = 0.51; // gap 0.02 < bandMinGap 0.40
    StationView f2 = sinkOf("f2");
    f2.inputs.push_back(StockSlotView("water", 50.0, 100.0)); // fill 0.50 (meio)
    WorldSnapshot snap2; snap2.readGateOpen = true; snap2.stations.push_back(f2);
    StockReport rep2; std::vector<std::size_t> pool2;
    sp2.evaluate(snap2, pool2, narrow, rep2);
    const ResourceVerdict* w2 = find(rep2, "water");
    CHECK(w2 != 0);
    // fill 0.50 no meio da banda clampada [~0.30, ~0.70] -> BAND, nao oscila.
    if (w2) CHECK_EQ(w2->tier, static_cast<int>(STK_LOW));
}

static void test_debounce() {
    WorldSnapshot snap; snap.readGateOpen = true;
    StationView f = sinkOf("f1");
    f.needsCritical.push_back(ItemNeed("iron", 1));
    snap.stations.push_back(f);
    StockPolicy sp; StockConfig cfg; cfg.demandConsistentTicks = 3;
    std::vector<std::size_t> pool; StockReport rep;

    sp.evaluate(snap, pool, cfg, rep);            // consec 1
    const ResourceVerdict* a = find(rep, "iron");
    CHECK(a && a->tier == STK_UNKNOWN && !a->stable);
    sp.evaluate(snap, pool, cfg, rep);            // consec 2
    const ResourceVerdict* b = find(rep, "iron");
    CHECK(b && b->tier == STK_UNKNOWN);
    sp.evaluate(snap, pool, cfg, rep);            // consec 3 -> estabiliza
    const ResourceVerdict* c = find(rep, "iron");
    CHECK(c && c->tier == STK_CRITICAL && c->stable);
}

static void test_observed_rise() {
    // A subida-observada agora vem do ESTOQUE DE STORAGE (snap.baseStock),
    // NAO do buffer de input da maquina (achado de calibracao 2026-07-11).
    WorldSnapshot snap; snap.readGateOpen = true;
    StationView f = mineOf("m1", "iron"); // produtor observado de "iron"
    snap.stations.push_back(f);
    snap.baseStockObserved = true;
    snap.baseStock["iron"] = 2.0;
    StockPolicy sp; StockConfig cfg; cfg.demandConsistentTicks = 1;
    std::vector<std::size_t> pool; StockReport rep;

    sp.evaluate(snap, pool, cfg, rep);            // 1a MEDICAO: semeia baseline, rise=0
    const ResourceVerdict* a = find(rep, "iron");
    CHECK(a && a->baseStock == 2.0 && a->riseSinceLast == 0.0);
    snap.baseStock["iron"] = 5.0;                 // deposito pousou: +3
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* b = find(rep, "iron");
    CHECK(b && b->riseSinceLast == 3.0);
    snap.baseStock["iron"] = 4.0;                 // consumo: -1
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* c = find(rep, "iron");
    CHECK(c && c->riseSinceLast == -1.0);

    // Rodada NAO observada (thread-adiada): sem sinal -> rise 0 e a referencia
    // NAO e atualizada (o proximo tick medido mede a variacao acumulada).
    snap.baseStockObserved = false;
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* d = find(rep, "iron");
    CHECK(d && d->riseSinceLast == 0.0 && d->baseStock == 0.0);
    snap.baseStockObserved = true;
    snap.baseStock["iron"] = 9.0;                 // subiu 4 -> 9 desde o ultimo medido (4)
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* e = find(rep, "iron");
    CHECK(e && e->riseSinceLast == 5.0);

    // Recurso AUSENTE do estoque medido (observed mas sem a chave) = SEM SINAL,
    // nao 0 duro: rise 0 e a referencia (9) intacta; ao reaparecer, mede o gap.
    snap.baseStock.erase("iron");                 // storage sumiu da varredura
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* r6 = find(rep, "iron");
    CHECK(r6 && r6->riseSinceLast == 0.0 && r6->baseStock == 0.0);
    snap.baseStock["iron"] = 12.0;                // reaparece: 12 - 9 = 3 (nao 12)
    sp.evaluate(snap, pool, cfg, rep);
    const ResourceVerdict* r7 = find(rep, "iron");
    CHECK(r7 && r7->riseSinceLast == 3.0);
}

static void test_would_staff_gates() {
    // iron HUNGRY, mina m1 produz iron (staffavel), 1 worker leve no pool.
    WorldSnapshot snap; snap.readGateOpen = true;
    StationView f = sinkOf("f1");
    f.needsCritical.push_back(ItemNeed("iron", 1));
    snap.stations.push_back(f);
    snap.stations.push_back(mineOf("m1", "iron"));
    snap.workers.push_back(workerOf("w1", 0.0, 100.0));   // leve
    std::vector<std::size_t> pool; pool.push_back(0);     // w1 e index 0

    StockPolicy sp; StockConfig cfg; cfg.demandConsistentTicks = 1;
    StockReport rep;
    sp.evaluate(snap, pool, cfg, rep);
    CHECK_EQ(rep.wouldStaff.size(), static_cast<std::size_t>(1));
    if (!rep.wouldStaff.empty()) {
        CHECK_EQ(rep.wouldStaff[0].mineId, std::string("m1"));
        CHECK_EQ(rep.wouldStaff[0].workerId, std::string("w1"));
    }

    // Agora o worker esta PESADO (ratio 0.6 >= 0.5) -> excluido, sem candidato.
    StockPolicy sp2;
    snap.workers[0].carryNow = 60.0;   // 60/100 = 0.6
    StockReport rep2;
    sp2.evaluate(snap, pool, cfg, rep2);
    CHECK_EQ(rep2.weightExcluded, 1);
    CHECK_EQ(rep2.wouldStaff.size(), static_cast<std::size_t>(1)); // mina staffavel...
    if (!rep2.wouldStaff.empty()) {
        CHECK(rep2.wouldStaff[0].workerId.empty());  // ...mas sem candidato apto
    }

    // Mina servida por permajob do jogador -> pulada (F8), nenhum would-staff.
    StockPolicy sp3;
    snap.workers[0].carryNow = 0.0;    // leve de novo
    WorkerView wperma; wperma.id = "wperma";
    PermajobView pj; pj.verb = WV_OPERATE_MACHINERY; pj.roleMachineId = "m1";
    wperma.permajobs.push_back(pj);
    snap.workers.push_back(wperma);    // index 1; NAO no pool
    StockReport rep3;
    sp3.evaluate(snap, pool, cfg, rep3);
    CHECK(rep3.wouldStaff.empty());    // m1 servida -> GOAP cuida
}

static void test_carry_not_observed_never_excludes() {
    // Peso NAO observado (rodada thread-adiada) nunca exclui por peso (inv.21):
    // loadRatio nao e confiavel -> observedHeavy=false mesmo com carryNow alto.
    WorldSnapshot snap; snap.readGateOpen = true;
    StationView f = sinkOf("f1");
    f.needsCritical.push_back(ItemNeed("iron", 1));
    snap.stations.push_back(f);
    snap.stations.push_back(mineOf("m1", "iron"));
    WorkerView w = workerOf("w1", 90.0, 100.0); // ratio 0.9 (pesadissimo)...
    w.carryObserved = false;                     // ...mas NAO observado
    snap.workers.push_back(w);
    std::vector<std::size_t> pool; pool.push_back(0);

    StockPolicy sp; StockConfig cfg; cfg.demandConsistentTicks = 1;
    StockReport rep;
    sp.evaluate(snap, pool, cfg, rep);
    CHECK_EQ(rep.weightExcluded, 0);             // peso nao observado -> nao exclui
    CHECK_EQ(rep.wouldStaff.size(), static_cast<std::size_t>(1));
    if (!rep.wouldStaff.empty()) {
        // sem gate de peso confiavel, o worker segue candidato (sombra)
        CHECK_EQ(rep.wouldStaff[0].workerId, std::string("w1"));
    }
}

static void test_read_gate_closed() {
    WorldSnapshot snap;   // readGateOpen = false
    StationView f = sinkOf("f1");
    f.needsCritical.push_back(ItemNeed("iron", 1));
    snap.stations.push_back(f);
    StockPolicy sp; StockConfig cfg; std::vector<std::size_t> pool; StockReport rep;
    sp.evaluate(snap, pool, cfg, rep);
    CHECK(rep.resources.empty());
    CHECK(rep.wouldStaff.empty());
}

int main() {
    test_aggregation_and_tiers();
    test_sated_when_no_topup();
    test_fill_fallback_clamp();
    test_debounce();
    test_observed_rise();
    test_would_staff_gates();
    test_carry_not_observed_never_excludes();
    test_read_gate_closed();

    std::cout << g_checks << " verificacoes, " << g_fails << " falhas\n";
    if (g_fails == 0) { std::cout << "OK\n"; return 0; }
    return 1;
}
