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
#include "domain/StockPolicy.h"

#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <cstddef>
#include <cmath>

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
StockPolicy        g_stock;   // parte 5: estado cross-tick (debounce/subida)
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

    // Rastreio de um personagem por nome (LS_M0_TRACK_WORKER): cargo, skill,
    // distancia e o MOTIVO de estar (ou nao) no pool despachavel. Prova ao
    // vivo que "papel = cargo", nao a acao corrente.
    if (LS_M0_TRACK_WORKER[0] != '\0') {
        for (std::size_t i = 0; i < snap.workers.size(); ++i) {
            const WorkerView& w = snap.workers[i];
            if (w.name != std::string(LS_M0_TRACK_WORKER)) {
                continue;
            }
            double dist = std::sqrt(distanceSquared(
                w.posX, w.posY, w.posZ, snap.baseX, snap.baseY, snap.baseZ));
            const char* motivo;
            if (w.isAnimal)                      motivo = "animal";
            else if (!w.isIdle)                  motivo = "ocupado";
            else if (w.hasPermajob())            motivo = "tem cargo (nao arrancamos)";
            else if (!w.isAuthorizableTarget())  motivo = "sem autoridade";
            else if (w.hungerBand >= HUNGER_KO)  motivo = "fome-KO";
            else if (dist > maxBaseDist)         motivo = "longe da base";
            else                                 motivo = "DESPACHAVEL";
            bool est = g_debounce.isStable(std::string("i:") + w.id);
            std::ostringstream s;
            s << "  RASTREIO \"" << w.name << "\": " << w.permajobs.size()
              << " cargo(s), " << (w.isIdle ? "ocioso" : "ocupado")
              << ", dist=" << static_cast<long>(dist) << "m -> " << motivo
              << (est ? " (ESTAVEL)" : "");
            diag::log(s.str());
            for (std::size_t j = 0; j < w.permajobs.size(); ++j) {
                std::ostringstream c;
                c << "    cargo[" << j << "]: verb=" << w.permajobs[j].verb
                  << " \"" << w.permajobs[j].roleName << "\"";
                diag::log(c.str());
            }
            break;
        }
    }

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

    // (9b) Parte 5 (P5-0, SOMBRA): StockPolicy -- demanda por recurso (banda
    // morta nativa + debounce), subida OBSERVADA do estoque, e as decisoes
    // would-staff (com gates de peso e servedBy). Emite NADA. "0 propostas de
    // operador" e correto quando o gargalo e logistica; aqui isso fica VISIVEL,
    // e o pulsar da demanda e o dado de calibracao da banda morta.
    if (LS_ENABLE_STOCK) {
        StockConfig scfg;
        scfg.critSinksForHungry = LS_RES_CRIT_SINKS_FOR_HUNGRY;
        scfg.fillMin = LS_RES_FILL_MIN;
        scfg.fillTarget = LS_RES_FILL_TARGET;
        scfg.bandMinGap = LS_RES_BAND_MIN_GAP;
        scfg.demandConsistentTicks = LS_RES_DEMAND_CONSISTENT_TICKS;
        scfg.staffMaxLoadRatio = LS_RES_STAFF_MAX_LOAD_RATIO;
        scfg.handsEmptyEps = LS_RES_HANDS_EMPTY_EPS;
        scfg.maxWouldStaffPerRound = LS_RES_MAX_WOULD_STAFF_PER_ROUND;
        StockReport rep;
        g_stock.evaluate(snap, pool0, scfg, rep);

        int prodStations = 0, observed = 0, permajobResolved = 0;
        for (std::size_t i = 0; i < snap.stations.size(); ++i) {
            if (snap.stations[i].workClass == WC_PRODUCTION) ++prodStations;
            if (snap.stations[i].prodObserved) ++observed;
        }
        // Quantos permajobs resolveram subject->maquina (valida F8/H3 ao vivo).
        for (std::size_t i = 0; i < snap.workers.size(); ++i) {
            const WorkerView& w = snap.workers[i];
            for (std::size_t j = 0; j < w.permajobs.size(); ++j) {
                if (!w.permajobs[j].roleMachineId.empty()) ++permajobResolved;
            }
        }
        {
            std::ostringstream s;
            s << "  ESTOQUE: " << prodStations << " producao (" << observed
              << " obs), " << rep.resources.size() << " recursos, "
              << rep.hungryResources << " HUNGRY, would-staff="
              << rep.wouldStaff.size() << " [sombra], peso-excl="
              << rep.weightExcluded << ", permajob->maquina res="
              << permajobResolved << ", base-stock="
              << (snap.baseStockObserved ? "lido" : "n/obs") << "("
              << snap.baseStock.size() << " itens)";
            diag::log(s.str());
        }
        // Detalhe por recurso (mais criticos primeiro): tier debounced, sinks,
        // produtores, fill, e a SUBIDA observada do estoque (dAmt).
        int sbudget = LS_RES_DETAIL_BUDGET;
        for (std::size_t i = 0; i < rep.resources.size() && sbudget > 0; ++i) {
            const ResourceVerdict& v = rep.resources[i];
            if (v.criticalSinks == 0 && v.topupSinks == 0) {
                continue; // so recursos com demanda declarada
            }
            --sbudget;
            // "medido" = a chave existe no estoque-da-base deste tick (deposito
            // vazio conta como medido 0); ausente = sem sinal de estoque ("?").
            bool measured = snap.baseStockObserved
                && snap.baseStock.find(v.itemKey) != snap.baseStock.end();
            std::ostringstream d;
            d << "    " << stockTierName(v.tier) << (v.stable ? "" : "?")
              << " " << v.itemKey
              << (v.itemName.empty() ? "" : (" (" + v.itemName + ")"))
              << ": crit=" << v.criticalSinks << " topup=" << v.topupSinks
              << " prod=" << v.producers << " stock=";
            if (measured) { d << v.baseStock << " dStock=" << v.riseSinceLast; }
            else          { d << "? dStock=?"; }
            d << " fill=" << v.fillRatio;
            diag::log(d.str());
        }
        // Decisoes would-staff (SOMBRA -- nada emitido).
        int wbudget = LS_M0_DETAIL_BUDGET;
        for (std::size_t i = 0; i < rep.wouldStaff.size() && wbudget > 0; ++i, --wbudget) {
            const StaffIntentShadow& si = rep.wouldStaff[i];
            std::ostringstream d;
            d << "    would-staff: recurso " << si.itemKey << " mina " << si.mineId
              << " worker \"" << (si.workerId.empty() ? std::string("--") : si.workerId)
              << "\" verbo=" << si.verb << " (" << si.reason << ")";
            diag::log(d.str());
        }
    }

    int budget = LS_M0_DETAIL_BUDGET;
    for (std::size_t i = 0; i < props.size() && budget > 0; ++i, --budget) {
        std::ostringstream s;
        s << "    proporia: \"" << props[i].worker << "\" -> operar posto "
          << props[i].station;
        diag::log(s.str());
    }
    // Pool despachavel (nomes) -- pra ver quem entra/sai (perto da base,
    // ocioso, apto, autorizado, sem cargo). Estavel = firme por N leituras.
    budget = LS_M0_DETAIL_BUDGET;
    for (std::size_t i = 0; i < pool0.size() && budget > 0; ++i, --budget) {
        const WorkerView& w = snap.workers[pool0[i]];
        bool est = g_debounce.isStable(std::string("i:") + w.id);
        std::ostringstream s;
        s << "    despachavel: \"" << w.name << "\" "
          << (est ? "ESTAVEL" : "instavel");
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
               << (s.prodObserved ? "" : "(nao-obs)")
               << " verbo=" << s.defaultVerb << " (task=" << s.defaultTaskNative << ")"
               << " ops=" << s.operatorsNow.size() << "/" << s.operatorsMax;
            diag::log(ls.str());
        }
    }
}

} // namespace pocs
} // namespace ls
