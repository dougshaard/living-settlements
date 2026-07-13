// Living Settlements -- domain/TaskBoard.cpp
// Classificacao de lacunas (secao 5.4). Puro, C++03, ASCII-only.
#include "domain/TaskBoard.h"
#include "domain/OperatorReconciler.h"  // operatorCapKey

namespace ls {
namespace domain {

namespace {

// Quem (se alguem) ja serve esta maquina via permajob -> GOAP cuida.
WorkerId servedByPermajob(const WorldSnapshot& snap, const StationId& station) {
    for (size_t i = 0; i < snap.workers.size(); ++i) {
        if (snap.workers[i].servesMachine(station)) {
            return snap.workers[i].id;
        }
    }
    return WorkerId(); // ""
}

void addGap(std::vector<Gap>& out, const StationView& s, GapType type, int lane,
            int verb, const std::string& resource, const WorkerId& servedBy) {
    Gap g;
    g.type = type;
    g.lane = lane;
    g.targetPostId = s.id;
    g.verb = verb;
    g.requiredStat = s.statUsed;
    g.servedBy = servedBy;
    g.key = makeTaskKey(s.id, verb, resource);
    g.posX = s.posX; g.posY = s.posY; g.posZ = s.posZ;
    out.push_back(g);
}

} // namespace

void buildGaps(const WorldSnapshot& snap, std::vector<Gap>& out) {
    out.clear();
    // Sem gate de leitura aberto, nada e acionavel (fail-closed; secao 5.2).
    if (!snap.readGateOpen) {
        return;
    }

    for (size_t i = 0; i < snap.stations.size(); ++i) {
        const StationView& s = snap.stations[i];

        // Ordem de classificacao (a primeira que casar vence). STARVED/
        // FULL/IMPOSSIBLE, quebra e falta de energia sao tratados ANTES da
        // lane OPERATOR -- por um operador numa maquina sem insumo/energia
        // e inutil (secao 5.4).
        if (s.broken) {
            addGap(out, s, GAP_REPAIR, LANE_BUILD, WV_REPAIR, "", WorkerId());
            continue;
        }
        if (!s.powerOk) {
            addGap(out, s, GAP_IMPOSSIBLE, LANE_POWER, WV_UNKNOWN, "", WorkerId());
            continue;
        }
        if (s.productionState == PROD_STARVED) {
            addGap(out, s, GAP_STARVED, LANE_LOGISTICS, WV_DELIVER_RESOURCES, "", WorkerId());
            continue;
        }
        if (s.productionState == PROD_FULL) {
            addGap(out, s, GAP_FULL, LANE_LOGISTICS, WV_DELIVER_RESOURCES, "", WorkerId());
            continue;
        }
        if (s.productionState == PROD_IMPOSSIBLE) {
            addGap(out, s, GAP_IMPOSSIBLE, LANE_LOGISTICS, WV_UNKNOWN, "", WorkerId());
            continue;
        }

        // Lane OPERATOR: precisa operar, tem vaga fisica livre, nao esta
        // dispensada, e um posto de PRODUCAO (achado do 1o run: nao despachar
        // para treino/gaiola/etc.), com producao comprovadamente NORMAL.
        // F1 / inv.21 (OBSERVACAO != VACANCIA): so numa rodada OBSERVADA
        // (thread-safe). Numa rodada nao observada, operatorsNow fica vazio e
        // productionState=UNKNOWN -> uma mina OCUPADA/STARVED pareceria vaga e
        // geraria uma lacuna falsa (briga-por-slot). Nao observado => nao emite.
        // O verbo vem da estacao (getDefaultTask, secao 9), nao hardcodado;
        // fallback WV_OPERATE_MACHINERY se a estacao nao mapeou o verbo.
        if (s.operatorsObserved && s.prodObserved
            && s.productionState == PROD_NORMAL
            && s.needsOperating && s.hasFreeSlot() && !s.dontNeedWork
            && s.workClass == WC_PRODUCTION) {
            int verb = (s.defaultVerb != WV_UNKNOWN)
                       ? s.defaultVerb : WV_OPERATE_MACHINERY;
            WorkerId served = servedByPermajob(snap, s.id);
            addGap(out, s, GAP_UNMANNED, LANE_OPERATOR, verb,
                   operatorCapKey(s.id), served);
        }
    }
}

void operatorGaps(const std::vector<Gap>& all, std::vector<Gap>& out) {
    out.clear();
    for (size_t i = 0; i < all.size(); ++i) {
        // So OPERATOR e ainda nao servida por um permajob (short-circuit).
        if (all[i].lane == LANE_OPERATOR && all[i].servedBy.empty()) {
            out.push_back(all[i]);
        }
    }
}

// Parte 5 (P5-0). ADITIVO: gera lacunas de LOGISTICA a partir do que cada
// estacao declara precisar. So estacoes OBSERVADAS (inv.21) contribuem --
// os need-lists sao lidos de consumptionItems, mutado em worker thread.
void buildStockGaps(const WorldSnapshot& snap, std::vector<Gap>& out) {
    out.clear();
    if (!snap.readGateOpen) {
        return; // fail-closed (secao 5.2)
    }
    for (size_t i = 0; i < snap.stations.size(); ++i) {
        const StationView& s = snap.stations[i];
        if (!s.prodObserved) {
            continue; // rodada nao observada: nada acionavel (inv.21)
        }
        // Criticos (< MIN): estoque essencialmente vazio deste insumo.
        for (size_t j = 0; j < s.needsCritical.size(); ++j) {
            addGap(out, s, GAP_PULL_CRITICAL, LANE_LOGISTICS,
                   WV_DELIVER_RESOURCES, s.needsCritical[j].itemKey, WorkerId());
            out.back().itemKey = s.needsCritical[j].itemKey;
        }
        // Topup (< ALVO, nao critico): Empty subset NotFull -> desconta os
        // criticos p/ nao duplicar. A folga MIN..ALVO e a banda morta (inv.16).
        for (size_t j = 0; j < s.needsTopup.size(); ++j) {
            const std::string& itk = s.needsTopup[j].itemKey;
            bool alsoCritical = false;
            for (size_t k = 0; k < s.needsCritical.size(); ++k) {
                if (s.needsCritical[k].itemKey == itk) {
                    alsoCritical = true;
                    break;
                }
            }
            if (alsoCritical) {
                continue;
            }
            addGap(out, s, GAP_PULL_TOPUP, LANE_LOGISTICS,
                   WV_DELIVER_RESOURCES, itk, WorkerId());
            out.back().itemKey = itk;
        }
    }
}

} // namespace domain
} // namespace ls
