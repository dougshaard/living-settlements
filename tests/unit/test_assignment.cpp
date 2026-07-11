// Living Settlements -- tests/unit/test_assignment.cpp
// Testa o casamento do estado-desejado: melhor-por-skill, reserva antes
// de propor (gate 5), sticky/dedup, rate-limit, e reserva cheia. Puro.
#include "domain/WorkModel.h"
#include "domain/Assignment.h"
#include "domain/OperatorReconciler.h"
#include "domain/ReservationManager.h"
#include "domain/IntentLedger.h"

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

static const int STAT = 7;

static WorkerView mkWorker(const WorkerId& id, int level) {
    WorkerView w; w.id = id; w.name = id; w.isIdle = true; w.canTakeOrders = true;
    w.skills.push_back(SkillLevel(STAT, level));
    return w;
}

static Gap mkGap(const StationId& s, int requiredStat) {
    Gap g; g.type = GAP_UNMANNED; g.lane = LANE_OPERATOR; g.targetPostId = s;
    g.requiredStat = requiredStat; g.verb = WV_OPERATE_MACHINERY;
    g.key = makeTaskKey(s, WV_OPERATE_MACHINERY, operatorCapKey(s));
    return g;
}

static void test_greedy_reserve_record() {
    WorldSnapshot snap; snap.readGateOpen = true;
    snap.workers.push_back(mkWorker("w1", 10));
    snap.workers.push_back(mkWorker("w2", 50));
    snap.workers.push_back(mkWorker("w3", 0));

    std::vector<Gap> gaps;
    gaps.push_back(mkGap("s1", STAT)); // usa skill 7
    gaps.push_back(mkGap("s2", -1));   // nao usa skill (empate -> menor id)
    gaps.push_back(mkGap("s3", STAT));

    ReservationManager rm;
    rm.setPhysical(operatorCapKey("s1"), 1);
    rm.setPhysical(operatorCapKey("s2"), 1);
    rm.setPhysical(operatorCapKey("s3"), 1);

    IntentLedger ledger;
    std::vector<std::size_t> pool; pool.push_back(0); pool.push_back(1); pool.push_back(2);

    AssignmentConfig cfg; cfg.maxPerRound = 5; cfg.leaseTTLHours = 100.0;
    std::vector<Proposal> out;
    proposeAssignments(snap, gaps, pool, rm, ledger, /*now*/0.0, cfg, out);

    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    // s1: melhor skill = w2 (50).
    CHECK_EQ(out[0].worker, std::string("w2"));
    CHECK_EQ(out[0].station, std::string("s1"));
    // s2: sem skill, empate w1/w3 -> menor id = w1.
    CHECK_EQ(out[1].worker, std::string("w1"));
    // s3: sobra w3.
    CHECK_EQ(out[2].worker, std::string("w3"));

    // Reservas adquiridas antes de propor.
    CHECK_EQ(rm.reserved(operatorCapKey("s1")), 1);
    CHECK_EQ(rm.reserved(operatorCapKey("s2")), 1);
    CHECK_EQ(rm.reserved(operatorCapKey("s3")), 1);
    // Intents registrados (PENDING).
    CHECK_EQ(ledger.activeCount(), static_cast<std::size_t>(3));
    // Pool consumido: cada worker usado uma vez.
    CHECK(pool.empty());

    // Sticky: rodar de novo NAO gera propostas nem novas reservas.
    std::vector<std::size_t> pool2; pool2.push_back(0); pool2.push_back(1); pool2.push_back(2);
    std::vector<Proposal> out2;
    proposeAssignments(snap, gaps, pool2, rm, ledger, /*now*/1.0, cfg, out2);
    CHECK(out2.empty());
    CHECK_EQ(ledger.activeCount(), static_cast<std::size_t>(3)); // inalterado
}

static void test_reservation_full_blocks() {
    WorldSnapshot snap; snap.readGateOpen = true;
    snap.workers.push_back(mkWorker("w1", 10));

    std::vector<Gap> gaps; gaps.push_back(mkGap("s1", STAT));

    ReservationManager rm;
    rm.setPhysical(operatorCapKey("s1"), 0); // fisico efetivo 0 (nativo ocupa)

    IntentLedger ledger;
    std::vector<std::size_t> pool; pool.push_back(0);
    AssignmentConfig cfg; cfg.maxPerRound = 5; cfg.leaseTTLHours = 100.0;
    std::vector<Proposal> out;
    proposeAssignments(snap, gaps, pool, rm, ledger, 0.0, cfg, out);

    CHECK(out.empty());                       // reserva falha -> nao propoe
    CHECK_EQ(ledger.activeCount(), static_cast<std::size_t>(0));
    CHECK_EQ(pool.size(), static_cast<std::size_t>(1)); // worker nao consumido
}

static void test_rate_limit() {
    WorldSnapshot snap; snap.readGateOpen = true;
    snap.workers.push_back(mkWorker("w1", 10));
    snap.workers.push_back(mkWorker("w2", 10));
    snap.workers.push_back(mkWorker("w3", 10));

    std::vector<Gap> gaps;
    gaps.push_back(mkGap("s1", STAT));
    gaps.push_back(mkGap("s2", STAT));
    gaps.push_back(mkGap("s3", STAT));

    ReservationManager rm;
    rm.setPhysical(operatorCapKey("s1"), 1);
    rm.setPhysical(operatorCapKey("s2"), 1);
    rm.setPhysical(operatorCapKey("s3"), 1);

    IntentLedger ledger;
    std::vector<std::size_t> pool; pool.push_back(0); pool.push_back(1); pool.push_back(2);
    AssignmentConfig cfg; cfg.maxPerRound = 1; cfg.leaseTTLHours = 100.0; // K=1
    std::vector<Proposal> out;
    proposeAssignments(snap, gaps, pool, rm, ledger, 0.0, cfg, out);

    CHECK_EQ(out.size(), static_cast<std::size_t>(1)); // so 1 por rodada
    CHECK_EQ(ledger.activeCount(), static_cast<std::size_t>(1));
    CHECK_EQ(pool.size(), static_cast<std::size_t>(2)); // 2 workers sobraram
}

// Mesma skill -> o mais PERTO do posto vence (desempate por distancia).
static void test_distance_tiebreak() {
    WorldSnapshot snap; snap.readGateOpen = true;
    WorkerView wa = mkWorker("wa", 10); wa.posX = 100.0; // mesma skill, longe
    WorkerView wb = mkWorker("wb", 10); wb.posX = 5.0;   // mesma skill, perto
    snap.workers.push_back(wa);
    snap.workers.push_back(wb);

    std::vector<Gap> gaps;
    Gap g = mkGap("s1", STAT);
    g.posX = 0.0; g.posY = 0.0; g.posZ = 0.0;            // posto na origem
    gaps.push_back(g);

    ReservationManager rm; rm.setPhysical(operatorCapKey("s1"), 1);
    IntentLedger ledger;
    std::vector<std::size_t> pool; pool.push_back(0); pool.push_back(1);
    AssignmentConfig cfg; cfg.maxPerRound = 5; cfg.leaseTTLHours = 100.0;
    std::vector<Proposal> out;
    proposeAssignments(snap, gaps, pool, rm, ledger, 0.0, cfg, out);

    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0].worker, std::string("wb")); // perto vence
}

int main() {
    test_greedy_reserve_record();
    test_reservation_full_blocks();
    test_rate_limit();
    test_distance_tiebreak();

    std::cout << g_checks << " verificacoes, " << g_fails << " falhas\n";
    if (g_fails == 0) { std::cout << "OK\n"; return 0; }
    return 1;
}
