// Living Settlements -- tests/unit/test_writefence.cpp
// Testa o predicado PURO writeGateOpen (core/LifecycleGate.h): a escrita so e
// permitida em MODE_OBSERVE_AND_ACT E com a cerca (WriteFence) ABERTA.
// Fail-closed em qualquer combinacao. Nao depende de KenshiLib (header-only).
//
//   g++ -std=c++03 -Wall -Wextra -I../../plugin/src test_writefence.cpp -o test_writefence
#include "core/LifecycleGate.h"

#include <iostream>

using namespace ls::core;

static int g_checks = 0;
static int g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
    std::cout << "FALHA linha " << __LINE__ << ": " #cond << "\n"; } } while (0)

static WriteFence openFence() {
    WriteFence f;
    f.open = true; f.signal = 0; f.saveState = 0;
    f.loading = false; f.resetting = false; f.threadsClear = true;
    return f;
}

static void test_both_open_allows() {
    CHECK(writeGateOpen(MODE_OBSERVE_AND_ACT, openFence()) == true);
}

static void test_mode_gates() {
    // Modo != OBSERVE_AND_ACT nunca escreve, mesmo com a cerca aberta.
    CHECK(writeGateOpen(MODE_SKIP, openFence()) == false);
    CHECK(writeGateOpen(MODE_OBSERVE_ONLY, openFence()) == false);
}

static void test_fence_gates() {
    // Cerca fechada (default) nunca escreve, mesmo em OBSERVE_AND_ACT.
    WriteFence closed;  // open = false por default (fail-closed)
    CHECK(writeGateOpen(MODE_OBSERVE_AND_ACT, closed) == false);

    // Cerca aberta mas com qualquer sinal de ciclo-de-vida marcado NAO importa
    // aqui (writeGateOpen olha so o bit .open ja composto por evaluateWriteFence);
    // mas se .open for false, e fail-closed.
    WriteFence f = openFence();
    f.open = false;   // evaluateWriteFence fechou (save/load/filas)
    CHECK(writeGateOpen(MODE_OBSERVE_AND_ACT, f) == false);
}

static void test_default_fence_is_closed() {
    // Invariante: uma WriteFence recem-construida e FECHADA (mundo ausente /
    // singleton -1). Nunca escrever "por omissao".
    WriteFence def;
    CHECK(def.open == false);
    CHECK(writeGateOpen(MODE_OBSERVE_AND_ACT, def) == false);
}

int main() {
    test_both_open_allows();
    test_mode_gates();
    test_fence_gates();
    test_default_fence_is_closed();

    std::cout << g_checks << " verificacoes, " << g_fails << " falhas\n";
    if (g_fails == 0) { std::cout << "OK\n"; return 0; }
    return 1;
}
