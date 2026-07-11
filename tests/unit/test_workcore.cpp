// Living Settlements -- tests/unit/test_workcore.cpp
// Testes do nucleo de trabalho (dominio puro): modelo de dados,
// TaskKey deterministico, e a formula canonica de reserva (secao 5.1)
// integrada ao ReservationManager. Compila em qualquer toolchain.
//
//   (uma linha) g++ -std=c++03 -Wall -Wextra -I../../plugin/src
//   ../../plugin/src/domain/WorkModel.cpp
//   ../../plugin/src/domain/OperatorReconciler.cpp
//   ../../plugin/src/domain/ReservationManager.cpp
//   test_workcore.cpp -o test_workcore  (depois: ./test_workcore)
#include "domain/WorkModel.h"
#include "domain/OperatorReconciler.h"
#include "domain/ReservationManager.h"

#include <iostream>
#include <vector>

using namespace ls::domain;

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #cond << "\n"; } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; if (!((a) == (b))) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #a " == " #b \
              << " (obtido " << (a) << " vs " << (b) << ")\n"; } } while (0)

// ---- Modelo: TaskKey deterministico (secao 3.1) ----
static void test_taskkey() {
    TaskKey a = makeTaskKey("b123", WV_OPERATE_MACHINERY, "cap:b123");
    TaskKey b = makeTaskKey("b123", WV_OPERATE_MACHINERY, "cap:b123");
    CHECK_EQ(a, b);                                   // mesma lacuna -> mesma chave
    CHECK(a != makeTaskKey("b124", WV_OPERATE_MACHINERY, "cap:b124"));
    CHECK(a != makeTaskKey("b123", WV_DELIVER_RESOURCES, "cap:b123"));
    CHECK(a != makeTaskKey("b123", WV_OPERATE_MACHINERY, "cap:b999"));
}

// ---- Modelo: helpers de WorkerView / MedicalGates / StationView ----
static void test_views() {
    WorkerView w;
    w.id = "w1"; w.name = "Beep";
    w.skills.push_back(SkillLevel(7 /*statId*/, 42 /*level*/));
    PermajobView pj; pj.verb = WV_OPERATE_MACHINERY; pj.roleMachineId = "b5";
    w.permajobs.push_back(pj);

    CHECK_EQ(w.skillFor(7), 42);
    CHECK_EQ(w.skillFor(99), -1);                     // stat desconhecido
    CHECK(w.servesMachine("b5"));
    CHECK(!w.servesMachine("b6"));
    CHECK(!w.servesMachine(""));                      // estacao vazia nunca casa

    MedicalGates m;
    CHECK(!m.incapacitated());                        // normal
    m.isUnconcious = true;
    CHECK(m.incapacitated());                         // KO = incapaz
    m.isUnconcious = false; m.proneState = 2 /*PS_CRIPPLED*/;
    CHECK(m.incapacitated());                         // prone anormal = incapaz

    StationView s;
    s.operatorsMax = 3;
    CHECK(s.hasFreeSlot());                            // 0 de 3
    s.operatorsNow.push_back("w1");
    s.operatorsNow.push_back("w2");
    s.operatorsNow.push_back("w3");
    CHECK(!s.hasFreeSlot());                           // 3 de 3, cheio
}

// ---- Formula canonica secao 5.1 (unidade) ----
static void test_effective_slots() {
    // Sem ocupantes: efetivo = max.
    CHECK_EQ(effectivePhysicalSlots(3, 0, 0), 3);
    // 1 ocupante nativo (nenhum nosso): desconta 1.
    CHECK_EQ(effectivePhysicalSlots(3, 1, 0), 2);
    // 2 ocupantes, 1 nosso: nativo=1, efetivo=2.
    CHECK_EQ(effectivePhysicalSlots(3, 2, 1), 2);
    // Todos ocupantes sao nossos: nativo=0, efetivo=max.
    CHECK_EQ(effectivePhysicalSlots(3, 3, 3), 3);
    // Satura em 0: mais ocupantes nativos que o max (mundo inconsistente).
    CHECK_EQ(effectivePhysicalSlots(2, 4, 0), 0);
    // Robustez: "nossos" > ocupantes nao produz negativo.
    CHECK_EQ(effectivePhysicalSlots(3, 1, 5), 3);
}

// ---- Formula secao 5.1 integrada ao ReservationManager ----
// available = max(0, efetivo - reservado) = max(0, (M-O) - Pending)
static void test_reconciler_integration() {
    ReservationManager rm;
    const StationId station = "b42";
    const ResourceKey cap = operatorCapKey(station);
    CHECK_EQ(cap, std::string("cap:b42"));

    // Estacao M=3, 1 ocupante nativo, 0 nossos => efetivo=2.
    rm.setPhysical(cap, effectivePhysicalSlots(3, 1, 0));
    CHECK_EQ(rm.physical(cap), 2);
    CHECK_EQ(rm.available(cap), 2);                   // 2 vagas para o mod oferecer

    // Reservamos 2 (um confirmado + um em transito) para a tarefa.
    std::vector<ReservationRequest> reqs;
    reqs.push_back(ReservationRequest(cap, "task:X", 2, /*expiresAt*/100.0));
    CHECK(rm.acquireAtomic(reqs, /*now*/0.0));
    CHECK_EQ(rm.reserved(cap), 2);
    CHECK_EQ(rm.available(cap), 0);                   // (M-O)-Pending = 1-1 = 0

    // Um nativo toma outro slot: O=2, ours=0 => efetivo=1. available satura.
    rm.setPhysical(cap, effectivePhysicalSlots(3, 2, 0));
    CHECK_EQ(rm.physical(cap), 1);
    CHECK_EQ(rm.available(cap), 0);                   // max(0, 1-2) = 0, nunca negativo

    // Terminal libera tudo do dono (secao 7.2 / invariante 9).
    CHECK_EQ(rm.releaseOwner("task:X"), 1);
    CHECK_EQ(rm.reserved(cap), 0);
    CHECK_EQ(rm.available(cap), 1);                   // volta ao efetivo corrente
}

static void test_distance() {
    CHECK_EQ(distanceSquared(0.0, 0.0, 0.0, 3.0, 4.0, 0.0), 25.0);
    CHECK_EQ(distanceSquared(1.0, 1.0, 1.0, 1.0, 1.0, 1.0), 0.0);
    CHECK_EQ(distanceSquared(0.0, 0.0, 0.0, 0.0, 0.0, 2.0), 4.0);
}

int main() {
    test_taskkey();
    test_views();
    test_effective_slots();
    test_reconciler_integration();
    test_distance();

    std::cout << g_checks << " verificacoes, " << g_fails << " falhas\n";
    if (g_fails == 0) {
        std::cout << "OK\n";
        return 0;
    }
    return 1;
}
