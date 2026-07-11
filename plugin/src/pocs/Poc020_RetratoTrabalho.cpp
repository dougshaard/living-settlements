// Living Settlements -- pocs/Poc020_RetratoTrabalho.cpp
// Marco 0: leitura OBSERVE_ONLY + dominio em SOMBRA. ASCII-only.
#include "pocs/Poc020_RetratoTrabalho.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"
#include "core/LifecycleGate.h"
#include "adapters/SnapshotBuilder.h"

#include "domain/WorkModel.h"
#include "domain/OperatorReconciler.h"
#include "domain/TaskBoard.h"
#include "domain/WorkerPool.h"
#include "domain/Debouncer.h"
#include "domain/IntentLedger.h"
#include "domain/Assignment.h"
#include "domain/ReservationManager.h"

#include <sstream>
#include <vector>
#include <string>
#include <cstddef>

namespace ls {
namespace pocs {

namespace {

using namespace ls::domain;

// Estado do coordenador que vive ENTRE rodadas (main thread; ADR-014).
// NAO persistido em disco (design 3.4). Reset total no LOAD entra no
// Marco 2 (o gate de leitura ja SKIPa durante o load, protegendo isto).
ReservationManager g_rm;
IntentLedger       g_ledger;
Debouncer          g_debounce(LS_M0_DEBOUNCE_N);
unsigned long      g_round = 0;

const char* prodName(int p) {
    switch (p) {
        case PROD_NORMAL:     return "NORMAL";
        case PROD_STARVED:    return "STARVED";
        case PROD_FULL:       return "FULL";
        case PROD_IMPOSSIBLE: return "IMPOSSIVEL";
        default:              return "?";
    }
}

} // namespace

void poc020Run(GameWorld* world) {
    ++g_round;

    // (1) Gate de LEITURA fail-closed (antes de qualquer enumeracao; 5.2).
    core::CoordMode mode = core::evaluateLifecycle(world, LS_M0_WRITES_ENABLED);
    if (mode == core::MODE_SKIP) {
        diag::log("M0: SKIP (load/reset/mundo ausente) -- nada lido");
        return;
    }
    bool tsafe = core::threadReadsSafe(world);

    // (2) Snapshot (unica fronteira de leitura).
    WorldSnapshot snap;
    if (!adapters::buildWorkSnapshot(world, tsafe, snap)) {
        diag::log("M0: sem base ancoravel neste tick");
        return;
    }

    // (3) Retrato do trabalho (contagens).
    int animals = 0, idle = 0, hungry = 0, incap = 0, withPermajob = 0;
    for (std::size_t i = 0; i < snap.workers.size(); ++i) {
        const WorkerView& w = snap.workers[i];
        if (w.isAnimal) ++animals;
        if (w.isIdle) ++idle;
        if (w.hungerBand >= HUNGER_REALLY_HUNGRY) ++hungry;
        if (w.medical.incapacitated()) ++incap;
        if (w.hasPermajob()) ++withPermajob;
    }
    int stStarved = 0, stFull = 0, stBroken = 0, stNoPower = 0, stUnmanned = 0;
    for (std::size_t i = 0; i < snap.stations.size(); ++i) {
        const StationView& s = snap.stations[i];
        if (s.productionState == PROD_STARVED) ++stStarved;
        if (s.productionState == PROD_FULL) ++stFull;
        if (s.broken) ++stBroken;
        if (!s.powerOk) ++stNoPower;
        if (s.needsOperating && s.hasFreeSlot() && !s.dontNeedWork) ++stUnmanned;
    }
    {
        std::ostringstream s;
        s << "M0 #" << g_round << " [" << core::modeName(mode)
          << (tsafe ? "" : ", thread-adiada") << "] t=" << snap.nowHours << "h -- "
          << snap.workers.size() << " personagens (" << animals << " animais, "
          << idle << " ociosos, " << withPermajob << " com cargo, " << hungry
          << " com fome, " << incap << " incapacitados); "
          << snap.stations.size() << " postos (" << stUnmanned << " sem operador, "
          << stStarved << " STARVED, " << stFull << " FULL, " << stBroken
          << " quebrados, " << stNoPower << " sem energia)";
        diag::log(s.str());
    }

    // (4) Reconciliar reservas (formula 5.1). Marco 0: nenhum operador
    // NOSSO confirmado ainda (escrita off) -> oursAmongCurrent = 0.
    for (std::size_t i = 0; i < snap.stations.size(); ++i) {
        const StationView& s = snap.stations[i];
        int eff = effectivePhysicalSlots(
            s.operatorsMax, static_cast<int>(s.operatorsNow.size()), 0);
        g_rm.setPhysical(operatorCapKey(s.id), eff);
    }
    g_rm.expire(snap.nowHours);

    // (5) Diff + debounce: so lacunas/ociosos estaveis por N leituras.
    std::vector<Gap> allGaps, opGaps0;
    buildGaps(snap, allGaps);
    operatorGaps(allGaps, opGaps0);
    // Proximidade: escala pelo raio da base (nao arrancar esquadrao destacado).
    double maxBaseDist = (snap.baseRadius > 0.0)
        ? snap.baseRadius * LS_M0_BASE_DIST_FACTOR : LS_M0_MAX_BASE_DIST;
    std::vector<std::size_t> pool0;
    buildPool(snap, maxBaseDist, pool0);

    std::vector<std::string> active;
    for (std::size_t i = 0; i < opGaps0.size(); ++i) {
        active.push_back(std::string("u:") + opGaps0[i].targetPostId);
    }
    for (std::size_t i = 0; i < pool0.size(); ++i) {
        active.push_back(std::string("i:") + snap.workers[pool0[i]].id);
    }
    g_debounce.observe(active);

    std::vector<Gap> opGaps;
    for (std::size_t i = 0; i < opGaps0.size(); ++i) {
        if (g_debounce.isStable(std::string("u:") + opGaps0[i].targetPostId)) {
            opGaps.push_back(opGaps0[i]);
        }
    }
    std::vector<std::size_t> pool;
    for (std::size_t i = 0; i < pool0.size(); ++i) {
        if (g_debounce.isStable(std::string("i:") + snap.workers[pool0[i]].id)) {
            pool.push_back(pool0[i]);
        }
    }
    // Contagens estaveis capturadas ANTES de o Assignment consumir o pool.
    const std::size_t stableGaps = opGaps.size();
    const std::size_t stablePool = pool.size();

    // (6) IntentLedger: confirma/expira e libera reservas terminais.
    std::vector<std::string> freed;
    g_ledger.reconcile(snap, snap.nowHours, LS_M0_GRACE_HOURS, freed);
    for (std::size_t i = 0; i < freed.size(); ++i) {
        g_rm.releaseOwner(freed[i]);
    }
    g_ledger.purgeTerminal();

    // (7/8) Estado-desejado em SOMBRA: casa, reserva, registra intent.
    // NADA e emitido (OrderEmitter off ate Marco 3).
    AssignmentConfig cfg;
    cfg.maxPerRound = LS_M0_MAX_PER_ROUND;
    cfg.leaseTTLHours = LS_M0_LEASE_TTL_HOURS;
    std::vector<Proposal> props;
    proposeAssignments(snap, opGaps, pool, g_rm, g_ledger, snap.nowHours, cfg, props);

    // (9) Painel "Por que?".
    {
        std::ostringstream s;
        s << "  SOMBRA: " << opGaps0.size() << " lacunas OPERATOR ("
          << stableGaps << " estaveis), pool " << pool0.size() << " ("
          << stablePool << " estaveis, prox<=" << maxBaseDist << "m), "
          << props.size() << " proposta(s) [NAO emitidas], ledger ativo="
          << g_ledger.activeCount();
        diag::log(s.str());
    }
    int budget = LS_M0_DETAIL_BUDGET;
    for (std::size_t i = 0; i < props.size() && budget > 0; ++i, --budget) {
        std::ostringstream s;
        s << "    proporia: \"" << props[i].worker << "\" -> operar posto "
          << props[i].station;
        diag::log(s.str());
    }
    // Alguns postos sem operador, para leitura humana do retrato.
    budget = LS_M0_DETAIL_BUDGET;
    for (std::size_t i = 0; i < snap.stations.size() && budget > 0; ++i) {
        const StationView& s = snap.stations[i];
        if (s.needsOperating && s.hasFreeSlot() && !s.dontNeedWork) {
            --budget;
            std::ostringstream ls;
            ls << "    posto sem operador: " << s.id << " fn=" << s.function
               << " prod=" << prodName(s.productionState)
               << " ops=" << s.operatorsNow.size() << "/" << s.operatorsMax;
            diag::log(ls.str());
        }
    }
}

} // namespace pocs
} // namespace ls
