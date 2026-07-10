// Living Settlements — tests/unit/test_reservation.cpp
// Pirâmide de testes §16.1 nível 1: núcleo puro fora do jogo.
// Sem framework: main() + verificação explícita, compila em qualquer
// toolchain (g++/clang/cl). Cada caso cita o requisito/cenário do doc.
//
// Compilar (exemplos):
//   g++ -std=c++03 -Wall -I../../plugin/src ../../plugin/src/domain/ReservationManager.cpp test_reservation.cpp -o test_reservation
//   cl /EHsc /W4 /I..\..\plugin\src ..\..\plugin\src\domain\ReservationManager.cpp test_reservation.cpp
#include "domain/ReservationManager.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <climits>

using namespace ls::domain;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const char* label) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("FALHOU: %s\n", label);
    }
}

static ReservationRequest req(const char* res, const char* owner, int qty, Tick exp) {
    return ReservationRequest(ResourceKey(res), OwnerKey(owner), qty, exp);
}

// REQ-LOG-002: disponível = físico − reservado.
static void test_available_basico() {
    ReservationManager m;
    m.setPhysical("item:cobre:silo1", 10);
    check(m.available("item:cobre:silo1") == 10, "REQ-LOG-002: disponivel inicial = fisico");

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:cobre:silo1", "task:T1", 4, 100.0));
    check(m.acquireAtomic(lote, 0.0), "REQ-LOG-002: aquisicao valida");
    check(m.reserved("item:cobre:silo1") == 4, "REQ-LOG-002: reservado = 4");
    check(m.available("item:cobre:silo1") == 6, "REQ-LOG-002: disponivel = 10-4");
    check(m.physical("item:cobre:silo1") == 10, "REQ-LOG-002: fisico inalterado");
}

// TEST-003: dois carregadores desejam o mesmo item — a soma reservada
// nunca excede a quantidade disponível.
static void test_dois_carregadores() {
    ReservationManager m;
    m.setPhysical("item:trigo:campo1", 10);

    std::vector<ReservationRequest> a;
    a.push_back(req("item:trigo:campo1", "task:carA", 7, 100.0));
    check(m.acquireAtomic(a, 0.0), "TEST-003: carregador A reserva 7");

    std::vector<ReservationRequest> b;
    b.push_back(req("item:trigo:campo1", "task:carB", 7, 100.0));
    check(!m.acquireAtomic(b, 0.0), "TEST-003: carregador B nao pode reservar 7 (so ha 3)");
    check(m.reserved("item:trigo:campo1") == 7, "TEST-003: soma reservada inalterada");

    std::vector<ReservationRequest> b2;
    b2.push_back(req("item:trigo:campo1", "task:carB", 3, 100.0));
    check(m.acquireAtomic(b2, 0.0), "TEST-003: carregador B reserva o resto (3)");
    check(m.reserved("item:trigo:campo1") == 10, "TEST-003: soma = fisico, nunca acima");
    check(m.available("item:trigo:campo1") == 0, "TEST-003: disponivel = 0");
}

// ADR-015: aquisição tudo-ou-nada — um pedido impossível no lote
// significa que NADA muda (rollback).
static void test_atomicidade_lote() {
    ReservationManager m;
    m.setPhysical("item:aco:forja", 5);
    m.setPhysical("cap:armazem2", 2);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:aco:forja", "task:T9", 3, 50.0)); // ok
    lote.push_back(req("cap:armazem2",  "task:T9", 3, 50.0)); // impossivel (2 < 3)
    check(!m.acquireAtomic(lote, 0.0), "ADR-015: lote com pedido impossivel falha");
    check(m.reserved("item:aco:forja") == 0, "ADR-015: rollback total (item)");
    check(m.reserved("cap:armazem2") == 0, "ADR-015: rollback total (capacidade)");
    check(m.leaseCount() == 0, "ADR-015: nenhum lease criado");

    // REQ-LOG-003: o mesmo lote com quantidades viáveis (item na origem
    // + capacidade no destino, mesmo dono/task) entra por inteiro.
    std::vector<ReservationRequest> ok;
    ok.push_back(req("item:aco:forja", "task:T9", 2, 50.0));
    ok.push_back(req("cap:armazem2",  "task:T9", 2, 50.0));
    check(m.acquireAtomic(ok, 0.0), "REQ-LOG-003: item+capacidade no mesmo lote");
    check(m.leaseCount() == 2, "REQ-LOG-003: dois leases do mesmo dono");
}

// Dois pedidos do MESMO lote sobre o MESMO recurso competem pela mesma
// quantidade (agregação na validação).
static void test_agregacao_mesmo_recurso() {
    ReservationManager m;
    m.setPhysical("item:pele:curtume", 5);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:pele:curtume", "task:T2", 3, 50.0));
    lote.push_back(req("item:pele:curtume", "task:T2", 3, 50.0)); // 3+3=6 > 5
    check(!m.acquireAtomic(lote, 0.0), "agregacao: 3+3 > 5 deve falhar como um todo");
    check(m.reserved("item:pele:curtume") == 0, "agregacao: nada reservado");
}

// Contrato de unicidade: pedidos viáveis do mesmo dono no mesmo recurso
// FUNDEM (quantity soma, expiresAt = maior). release é não-ambíguo.
static void test_fusao_mesmo_dono() {
    ReservationManager m;
    m.setPhysical("item:couro:bancada", 5);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:couro:bancada", "task:T3", 2, 50.0));
    lote.push_back(req("item:couro:bancada", "task:T3", 3, 60.0));
    check(m.acquireAtomic(lote, 0.0), "fusao: 2+3 <= 5 entra");
    check(m.leaseCount() == 1, "fusao: UM lease por (recurso,dono)");
    check(m.reserved("item:couro:bancada") == 5, "fusao: quantity somada");

    check(m.expire(50.0) == 0, "fusao: expiresAt = max(50,60), nao expira em 50");
    check(m.expire(60.0) == 1, "fusao: expira em 60");

    // Fusão também com lease PRÉ-existente (lotes separados)
    std::vector<ReservationRequest> a;
    a.push_back(req("item:couro:bancada", "task:T3", 2, 100.0));
    std::vector<ReservationRequest> b;
    b.push_back(req("item:couro:bancada", "task:T3", 3, 200.0));
    check(m.acquireAtomic(a, 0.0) && m.acquireAtomic(b, 0.0), "fusao: lotes separados");
    check(m.leaseCount() == 1, "fusao: continua um lease");
    check(m.reserved("item:couro:bancada") == 5, "fusao: 2+3 de novo");
    check(m.release("item:couro:bancada", "task:T3"), "fusao: um release libera tudo");
    check(!m.release("item:couro:bancada", "task:T3"), "fusao: segundo release e no-op");
    check(m.available("item:couro:bancada") == 5, "fusao: tudo de volta");
}

// Donos DIFERENTES no mesmo recurso mantêm leases separados.
static void test_dois_donos_mesmo_recurso() {
    ReservationManager m;
    m.setPhysical("item:tecido:tear", 5);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:tecido:tear", "task:A", 2, 100.0));
    lote.push_back(req("item:tecido:tear", "task:B", 3, 100.0));
    check(m.acquireAtomic(lote, 0.0), "dois donos: 2+3 <= 5 entra");
    check(m.leaseCount() == 2, "dois donos: dois leases");
    check(m.release("item:tecido:tear", "task:A"), "dois donos: solta A");
    check(m.reserved("item:tecido:tear") == 3, "dois donos: B intacto");
    check(m.ownerHasLeases("task:B"), "dois donos: B segue dono");
}

// Guarda de overflow: somas que estourariam int falham o lote inteiro.
static void test_overflow() {
    ReservationManager m;
    m.setPhysical("item:big:x", INT_MAX);

    std::vector<ReservationRequest> mesmoDono;
    mesmoDono.push_back(req("item:big:x", "task:T", INT_MAX, 100.0));
    mesmoDono.push_back(req("item:big:x", "task:T", 2, 100.0));
    check(!m.acquireAtomic(mesmoDono, 0.0), "overflow: fusao INT_MAX+2 falha");
    check(m.leaseCount() == 0, "overflow: nada reservado (mesmo dono)");

    std::vector<ReservationRequest> doisDonos;
    doisDonos.push_back(req("item:big:x", "task:T1", INT_MAX, 100.0));
    doisDonos.push_back(req("item:big:x", "task:T2", 2, 100.0));
    check(!m.acquireAtomic(doisDonos, 0.0), "overflow: soma entre donos falha");
    check(m.leaseCount() == 0, "overflow: nada reservado (dois donos)");
}

// §6.4 regra 6: nenhuma reserva sem lease e expiração; expire() libera.
static void test_expiracao() {
    ReservationManager m;
    m.setPhysical("bed:hospital1", 1);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("bed:hospital1", "task:med1", 1, 10.0));
    check(m.acquireAtomic(lote, 5.0), "expiracao: lease valido em t=5");

    check(m.expire(9.9) == 0, "expiracao: nada expira antes do prazo");
    check(m.available("bed:hospital1") == 0, "expiracao: cama segue reservada");

    check(m.expire(10.0) == 1, "expiracao: lease expira em t>=10");
    check(m.available("bed:hospital1") == 1, "expiracao: cama liberada");

    // lease ja vencido na aquisicao e rejeitado
    std::vector<ReservationRequest> vencido;
    vencido.push_back(req("bed:hospital1", "task:med2", 1, 10.0));
    check(!m.acquireAtomic(vencido, 10.0), "expiracao: lease sem validade futura rejeitado");
}

// §7.2: todo estado terminal libera seus ativos (release por dono).
static void test_release_por_dono() {
    ReservationManager m;
    m.setPhysical("item:agua:poco", 8);
    m.setPhysical("cap:cozinha", 4);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:agua:poco", "task:T5", 4, 100.0));
    lote.push_back(req("cap:cozinha",   "task:T5", 4, 100.0));
    check(m.acquireAtomic(lote, 0.0), "release-dono: setup");
    check(m.ownerHasLeases("task:T5"), "release-dono: dono tem leases");

    check(m.releaseOwner("task:T5") == 2, "release-dono: 2 leases liberados");
    check(!m.ownerHasLeases("task:T5"), "release-dono: nenhum lease preso (PRINC-005)");
    check(m.available("item:agua:poco") == 8, "release-dono: item liberado");
    check(m.available("cap:cozinha") == 4, "release-dono: capacidade liberada");
}

// release individual e reaquisição.
static void test_release_individual() {
    ReservationManager m;
    m.setPhysical("item:carne:acougue", 3);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:carne:acougue", "task:T7", 3, 100.0));
    check(m.acquireAtomic(lote, 0.0), "release: setup");
    check(!m.release("item:carne:acougue", "task:OUTRO"), "release: dono errado nao libera");
    check(m.release("item:carne:acougue", "task:T7"), "release: dono correto libera");
    check(!m.release("item:carne:acougue", "task:T7"), "release: segundo release e no-op");
    check(m.acquireAtomic(lote, 0.0), "release: reaquisicao apos liberar");
}

// Pedidos malformados nunca alteram estado.
static void test_pedidos_invalidos() {
    ReservationManager m;
    m.setPhysical("item:x:y", 5);

    std::vector<ReservationRequest> vazio;
    check(!m.acquireAtomic(vazio, 0.0), "invalido: lote vazio falha");

    std::vector<ReservationRequest> qzero;
    qzero.push_back(req("item:x:y", "task:T", 0, 100.0));
    check(!m.acquireAtomic(qzero, 0.0), "invalido: quantidade 0 falha");

    std::vector<ReservationRequest> qneg;
    qneg.push_back(req("item:x:y", "task:T", -2, 100.0));
    check(!m.acquireAtomic(qneg, 0.0), "invalido: quantidade negativa falha");

    std::vector<ReservationRequest> semdono;
    semdono.push_back(req("item:x:y", "", 1, 100.0));
    check(!m.acquireAtomic(semdono, 0.0), "invalido: dono vazio falha");

    std::vector<ReservationRequest> semres;
    semres.push_back(req("", "task:T", 1, 100.0));
    check(!m.acquireAtomic(semres, 0.0), "invalido: recurso vazio falha");

    check(m.leaseCount() == 0, "invalido: estado intocado");
}

// TEST-007 (parcial, camada de domínio): reconciliação para baixo — o
// mundo mudou por fora e o físico caiu abaixo do reservado; available
// satura em zero e nunca fica negativo.
static void test_reconciliacao_para_baixo() {
    ReservationManager m;
    m.setPhysical("item:z:w", 10);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:z:w", "task:T8", 8, 100.0));
    check(m.acquireAtomic(lote, 0.0), "reconciliacao: setup");

    m.setPhysical("item:z:w", 4); // snapshot observou só 4 no mundo
    check(m.available("item:z:w") == 0, "reconciliacao: available satura em 0");
    check(m.reserved("item:z:w") == 8, "reconciliacao: leases intactos ate replanejar");

    std::vector<ReservationRequest> extra;
    extra.push_back(req("item:z:w", "task:T9", 1, 100.0));
    check(!m.acquireAtomic(extra, 0.0), "reconciliacao: nada novo cabe");

    // Reconciliação para CIMA: o físico volta a subir e o available
    // reflete físico - reservado imediatamente.
    m.setPhysical("item:z:w", 12);
    check(m.available("item:z:w") == 4, "reconciliacao: 12-8=4 disponivel");
    std::vector<ReservationRequest> quatro;
    quatro.push_back(req("item:z:w", "task:T9", 4, 100.0));
    check(m.acquireAtomic(quatro, 0.0), "reconciliacao: 4 cabe apos subir");
    std::vector<ReservationRequest> cinco;
    cinco.push_back(req("item:z:w", "task:T10", 1, 100.0));
    check(!m.acquireAtomic(cinco, 0.0), "reconciliacao: 13o nao cabe");
}

// Expiração PARCIAL: só os vencidos saem, atravessando recursos.
static void test_expiracao_parcial() {
    ReservationManager m;
    m.setPhysical("res:R1", 5);
    m.setPhysical("res:R2", 1);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("res:R1", "task:T1", 2, 10.0));
    lote.push_back(req("res:R1", "task:T2", 1, 20.0));
    lote.push_back(req("res:R1", "task:T3", 1, 10.0));
    lote.push_back(req("res:R2", "task:T4", 1, 10.0));
    check(m.acquireAtomic(lote, 0.0), "exp-parcial: setup 4 leases");
    check(m.leaseCount() == 4, "exp-parcial: 4 leases ativos");

    check(m.expire(10.0) == 3, "exp-parcial: 3 vencem em t=10 (T1,T3,T4)");
    check(m.reserved("res:R1") == 1, "exp-parcial: so T2 resta em R1");
    check(m.available("res:R1") == 4, "exp-parcial: 4 liberados em R1");
    check(m.available("res:R2") == 1, "exp-parcial: R2 todo livre");

    std::vector<ReservationRequest> reuso;
    reuso.push_back(req("res:R1", "task:T5", 4, 100.0));
    check(m.acquireAtomic(reuso, 50.0), "exp-parcial: quantidade liberada readquirivel");
}

// Entidade Reservation (§7) tem 'version' — monotônica por recurso.
static void test_version_monotonica() {
    ReservationManager m;
    m.setPhysical("res:V", 5); // v1

    std::vector<ReservationRequest> lote;
    lote.push_back(req("res:V", "task:T", 2, 100.0)); // v2
    check(m.acquireAtomic(lote, 0.0), "version: acquire");

    std::vector<std::string> l1;
    m.describe(l1);
    check(l1.size() == 1 && l1[0].find("v2") != std::string::npos,
          "version: v2 apos setPhysical+acquire");

    check(m.release("res:V", "task:T"), "version: release"); // v3
    check(m.acquireAtomic(lote, 0.0), "version: re-acquire"); // v4

    std::vector<std::string> l2;
    m.describe(l2);
    check(l2.size() == 1 && l2[0].find("v4") != std::string::npos,
          "version: v4 apos release+re-acquire");
}

// describe com múltiplos recursos: uma linha por lease, todos citados.
static void test_describe_multi() {
    ReservationManager m;
    m.setPhysical("res:A", 5);
    m.setPhysical("res:B", 5);

    std::vector<ReservationRequest> lote;
    lote.push_back(req("res:A", "task:T1", 1, 100.0));
    lote.push_back(req("res:A", "task:T2", 2, 100.0));
    lote.push_back(req("res:B", "task:T3", 3, 100.0));
    check(m.acquireAtomic(lote, 0.0), "describe-multi: setup");

    std::vector<std::string> linhas;
    m.describe(linhas);
    check(linhas.size() == 3, "describe-multi: 3 linhas (uma por lease)");
    bool temA = false, temB = false;
    for (size_t i = 0; i < linhas.size(); ++i) {
        if (linhas[i].find("res:A") != std::string::npos) temA = true;
        if (linhas[i].find("res:B") != std::string::npos) temB = true;
    }
    check(temA && temB, "describe-multi: ambos os recursos citados");
}

// Consultas sobre recursos/donos inexistentes: zeros e no-ops.
static void test_consultas_inexistentes() {
    ReservationManager m;
    check(m.physical("nao:existe") == 0, "inexistente: physical 0");
    check(m.reserved("nao:existe") == 0, "inexistente: reserved 0");
    check(m.available("nao:existe") == 0, "inexistente: available 0");
    check(!m.release("nao:existe", "task:X"), "inexistente: release false");
    check(m.releaseOwner("task:ninguem") == 0, "inexistente: releaseOwner 0");
    check(!m.ownerHasLeases("task:ninguem"), "inexistente: sem leases");
    check(m.expire(1e9) == 0, "inexistente: expire 0");
    check(m.leaseCount() == 0, "inexistente: leaseCount 0");
}

// GOAL-007 / POC-010: describe produz uma linha por lease.
static void test_describe() {
    ReservationManager m;
    m.setPhysical("item:a:b", 5);
    std::vector<ReservationRequest> lote;
    lote.push_back(req("item:a:b", "task:T10", 2, 100.0));
    m.acquireAtomic(lote, 0.0);

    std::vector<std::string> linhas;
    m.describe(linhas);
    check(linhas.size() == 1, "describe: uma linha por lease");
    check(linhas[0].find("task:T10") != std::string::npos, "describe: cita o dono");
    check(linhas[0].find("qty=2") != std::string::npos, "describe: cita a quantidade");
}

int main() {
    test_available_basico();
    test_dois_carregadores();
    test_atomicidade_lote();
    test_agregacao_mesmo_recurso();
    test_fusao_mesmo_dono();
    test_dois_donos_mesmo_recurso();
    test_overflow();
    test_expiracao();
    test_release_por_dono();
    test_release_individual();
    test_pedidos_invalidos();
    test_reconciliacao_para_baixo();
    test_expiracao_parcial();
    test_version_monotonica();
    test_describe_multi();
    test_consultas_inexistentes();
    test_describe();

    std::printf("%d verificacoes, %d falhas\n", g_checks, g_failures);
    if (g_failures != 0)
        return 1;
    std::printf("OK\n");
    return 0;
}
