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
        // dispensada, producao NORMAL/UNKNOWN com energia, E e um posto de
        // PRODUCAO (achado do 1o run: nao despachar para treino/gaiola/etc.).
        if (s.needsOperating && s.hasFreeSlot() && !s.dontNeedWork
            && s.workClass == WC_PRODUCTION) {
            WorkerId served = servedByPermajob(snap, s.id);
            addGap(out, s, GAP_UNMANNED, LANE_OPERATOR, WV_OPERATE_MACHINERY,
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

} // namespace domain
} // namespace ls
