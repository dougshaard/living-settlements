// Living Settlements -- tests/unit/test_intent.cpp
// Testes de Debouncer (estabilidade por N leituras) e IntentLedger
// (verificar-antes-de-confiar: CONFIRM/FAILED/DONE + liberacao). Puro.
#include "domain/WorkModel.h"
#include "domain/Debouncer.h"
#include "domain/IntentLedger.h"

#include <iostream>
#include <vector>
#include <string>

using namespace ls::domain;

static int g_checks = 0;
static int g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #cond << "\n"; } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; if (!((a) == (b))) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #a " == " #b \
              << " (obtido " << (a) << " vs " << (b) << ")\n"; } } while (0)

static std::vector<std::string> keys1(const std::string& a) {
    std::vector<std::string> v; v.push_back(a); return v;
}

static void test_debouncer() {
    Debouncer d(3);

    d.observe(keys1("a"));  CHECK_EQ(d.count("a"), 1); CHECK(!d.isStable("a"));
    d.observe(keys1("a"));  CHECK_EQ(d.count("a"), 2); CHECK(!d.isStable("a"));
    d.observe(keys1("a"));  CHECK_EQ(d.count("a"), 3); CHECK(d.isStable("a"));
    d.observe(keys1("a"));  CHECK_EQ(d.count("a"), 3);   // satura no limiar
    CHECK(d.isStable("a"));

    // Interrupcao zera: um tick sem "a" -> volta a 1 na proxima.
    std::vector<std::string> empty;
    d.observe(empty);       CHECK_EQ(d.count("a"), 0); CHECK(!d.isStable("a"));
    d.observe(keys1("a"));  CHECK_EQ(d.count("a"), 1); CHECK(!d.isStable("a"));

    // Chaves ausentes num tick somem do rastreio.
    std::vector<std::string> ab; ab.push_back("a"); ab.push_back("b");
    d.observe(ab);
    CHECK_EQ(d.trackedCount(), static_cast<std::size_t>(2));
    d.observe(keys1("b"));  // "a" sai
    CHECK_EQ(d.count("a"), 0);
    CHECK_EQ(d.trackedCount(), static_cast<std::size_t>(1));

    // threshold <= 1 => estavel na primeira.
    Debouncer d1(1);
    d1.observe(keys1("x")); CHECK(d1.isStable("x"));
}

static Intent mkIntent(const WorkerId& w, const TaskKey& t, const StationId& s,
                       Tick emitted) {
    Intent in;
    in.workerId = w; in.taskKey = t; in.targetStation = s;
    in.verb = IV_ADD_ORDER; in.emittedHours = emitted;
    in.reservationOwner = std::string("task:") + t;
    return in;
}

static WorldSnapshot snapWith(const StationId& id, bool needsOp,
                              const WorkerId& op /* "" = vazio */) {
    WorldSnapshot snap; snap.readGateOpen = true;
    StationView s; s.id = id; s.needsOperating = needsOp; s.operatorsMax = 1;
    if (!op.empty()) s.operatorsNow.push_back(op);
    snap.stations.push_back(s);
    return snap;
}

static void test_ledger_confirm_done() {
    IntentLedger led;
    led.record(mkIntent("w1", "T1", "s1", /*emitted*/0.0));
    CHECK(led.hasActive("w1", "T1"));
    CHECK(led.taskHasActive("T1"));
    CHECK_EQ(led.activeCount(), static_cast<std::size_t>(1));

    std::vector<std::string> freed;

    // w1 virou operador -> CONFIRMED, nada liberado.
    WorldSnapshot s1 = snapWith("s1", true, "w1");
    led.reconcile(s1, /*now*/1.0, /*grace*/5.0, freed);
    CHECK(freed.empty());
    CHECK_EQ(led.activeCount(), static_cast<std::size_t>(1)); // CONFIRMED ainda ativo

    // Estacao nao precisa mais operar -> DONE + libera a reserva.
    WorldSnapshot s2 = snapWith("s1", false, "w1");
    led.reconcile(s2, /*now*/2.0, /*grace*/5.0, freed);
    CHECK_EQ(freed.size(), static_cast<std::size_t>(1));
    if (!freed.empty()) CHECK_EQ(freed[0], std::string("task:T1"));

    led.purgeTerminal();
    CHECK_EQ(led.count(), static_cast<std::size_t>(0));
    CHECK(!led.hasActive("w1", "T1"));
}

static void test_ledger_fail_by_grace() {
    IntentLedger led;
    led.record(mkIntent("w2", "T2", "s2", /*emitted*/0.0));
    std::vector<std::string> freed;

    // Worker nunca virou operador e a graca esgotou -> FAILED + libera.
    WorldSnapshot s = snapWith("s2", true, ""); // sem operador
    led.reconcile(s, /*now*/10.0, /*grace*/5.0, freed);
    CHECK_EQ(freed.size(), static_cast<std::size_t>(1));
    if (!freed.empty()) CHECK_EQ(freed[0], std::string("task:T2"));
    CHECK_EQ(led.activeCount(), static_cast<std::size_t>(0));
}

static void test_ledger_confirmed_then_left() {
    IntentLedger led;
    led.record(mkIntent("w3", "T3", "s3", 0.0));
    std::vector<std::string> freed;

    WorldSnapshot conf = snapWith("s3", true, "w3");
    led.reconcile(conf, 1.0, 5.0, freed);
    CHECK(freed.empty());                       // CONFIRMED

    // Worker saiu do posto (ainda precisa operar) -> FAILED + reabre.
    WorldSnapshot left = snapWith("s3", true, "");
    led.reconcile(left, 2.0, 5.0, freed);
    CHECK_EQ(freed.size(), static_cast<std::size_t>(1));
    if (!freed.empty()) CHECK_EQ(freed[0], std::string("task:T3"));
}

static void test_ledger_dedup_no_reemit() {
    IntentLedger led;
    led.record(mkIntent("w4", "T4", "s4", 0.0));
    // Enquanto ativo, hasActive impede reemissao (invariante 13).
    CHECK(led.hasActive("w4", "T4"));
    CHECK(led.taskHasActive("T4"));
    CHECK(!led.hasActive("w9", "T4")); // outro worker, mesma task: nao e o mesmo par
}

int main() {
    test_debouncer();
    test_ledger_confirm_done();
    test_ledger_fail_by_grace();
    test_ledger_confirmed_then_left();
    test_ledger_dedup_no_reemit();

    std::cout << g_checks << " verificacoes, " << g_fails << " falhas\n";
    if (g_fails == 0) { std::cout << "OK\n"; return 0; }
    return 1;
}
