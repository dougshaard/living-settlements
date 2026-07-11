// Living Settlements -- domain/WorkerPool.cpp
// Filtro de workers despachaveis. Puro, C++03, ASCII-only.
#include "domain/WorkerPool.h"

namespace ls {
namespace domain {

bool isDispatchable(const WorkerView& w) {
    if (w.isAnimal) {
        return false;                 // boi ocioso nao vira carregador (secao 7.2)
    }
    if (!w.isIdle) {
        return false;
    }
    if (w.hasPermajob()) {
        return false;                 // tem papel -> deixar ao GOAP
    }
    if (!w.isAuthorizableTarget()) {
        return false;                 // autoridade do jogador e sagrada
    }
    if (w.hungerBand >= HUNGER_KO) {
        return false;                 // KO/colapso de fome nao trabalha
    }
    return true;
}

void buildPool(const WorldSnapshot& snap, double maxBaseDist,
               std::vector<std::size_t>& outIdx) {
    outIdx.clear();
    if (!snap.readGateOpen) {
        return;                       // fail-closed (secao 5.2)
    }
    const bool useProximity = (maxBaseDist > 0.0);
    const double maxDistSq = maxBaseDist * maxBaseDist;
    for (std::size_t i = 0; i < snap.workers.size(); ++i) {
        const WorkerView& w = snap.workers[i];
        if (!isDispatchable(w)) {
            continue;
        }
        if (useProximity) {
            double d2 = distanceSquared(w.posX, w.posY, w.posZ,
                                        snap.baseX, snap.baseY, snap.baseZ);
            if (d2 > maxDistSq) {
                continue;             // esquadrao destacado -> nao despachar
            }
        }
        outIdx.push_back(i);
    }
}

} // namespace domain
} // namespace ls
