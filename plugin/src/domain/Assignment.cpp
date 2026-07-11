// Living Settlements -- domain/Assignment.cpp
// Puro, C++03, ASCII-only.
#include "domain/Assignment.h"
#include "domain/OperatorReconciler.h"  // operatorCapKey

namespace ls {
namespace domain {

namespace {

// Skill efetivo do worker para a lacuna. requiredStat < 0 => o posto nao
// usa skill: todos empatam em 0. Skill desconhecido (-1) conta como 0.
int effectiveSkill(const WorkerView& w, int requiredStat) {
    if (requiredStat < 0) {
        return 0;
    }
    int lvl = w.skillFor(requiredStat);
    return (lvl < 0) ? 0 : lvl;
}

// Escolhe, dentre poolIdx, a POSICAO (no vetor poolIdx) do melhor worker
// para a lacuna, por ordem lexicografica: (1) maior skill; (2) MAIS PERTO
// do posto; (3) menor id (determinismo). Retorna poolIdx.size() se vazio.
std::size_t pickBest(const WorldSnapshot& snap,
                     const std::vector<std::size_t>& poolIdx,
                     const Gap& g) {
    std::size_t best = poolIdx.size();
    int bestSkill = -1;
    double bestDist = 0.0;
    for (std::size_t p = 0; p < poolIdx.size(); ++p) {
        const WorkerView& w = snap.workers[poolIdx[p]];
        int sk = effectiveSkill(w, g.requiredStat);
        double d2 = distanceSquared(w.posX, w.posY, w.posZ,
                                    g.posX, g.posY, g.posZ);
        bool better;
        if (best >= poolIdx.size()) {
            better = true;
        } else if (sk != bestSkill) {
            better = (sk > bestSkill);
        } else if (d2 != bestDist) {
            better = (d2 < bestDist);          // mesma skill: mais perto vence
        } else {
            better = (w.id < snap.workers[poolIdx[best]].id);
        }
        if (better) {
            best = p;
            bestSkill = sk;
            bestDist = d2;
        }
    }
    return best;
}

} // namespace

void proposeAssignments(const WorldSnapshot& snap,
                        const std::vector<Gap>& opGaps,
                        std::vector<std::size_t>& poolIdx,
                        ReservationManager& rm,
                        IntentLedger& ledger,
                        Tick now,
                        const AssignmentConfig& cfg,
                        std::vector<Proposal>& out) {
    out.clear();
    int made = 0;

    for (std::size_t gi = 0; gi < opGaps.size(); ++gi) {
        if (made >= cfg.maxPerRound) {
            break;                                   // rate-limit (histerese)
        }
        const Gap& g = opGaps[gi];

        // Sticky/dedup: ja existe intent ativo para esta tarefa -> nao mexe.
        if (ledger.taskHasActive(g.key)) {
            continue;
        }
        if (poolIdx.empty()) {
            break;                                   // ninguem para despachar
        }

        std::size_t pick = pickBest(snap, poolIdx, g);
        if (pick >= poolIdx.size()) {
            continue;
        }
        const WorkerId workerId = snap.workers[poolIdx[pick]].id;

        // RESERVA ANTES de propor (gate 5). Tudo-ou-nada; falhou => pula.
        const ResourceKey cap = operatorCapKey(g.targetPostId);
        const OwnerKey owner = std::string("task:") + g.key;
        std::vector<ReservationRequest> reqs;
        reqs.push_back(ReservationRequest(cap, owner, 1, now + cfg.leaseTTLHours));
        if (!rm.acquireAtomic(reqs, now)) {
            continue;                                // slot cheio (fisico efetivo 0)
        }

        // Registra o intent (PENDING). Em SOMBRA nao ha emissao; o
        // OrderEmitter (adapter) e quem, no Marco 3, efetiva a escrita.
        Intent in;
        in.workerId = workerId;
        in.taskKey = g.key;
        in.targetStation = g.targetPostId;
        in.verb = IV_ADD_ORDER;
        in.emittedHours = now;
        in.reservationOwner = owner;
        ledger.record(in);

        Proposal p;
        p.worker = workerId;
        p.taskKey = g.key;
        p.station = g.targetPostId;
        p.verb = g.verb;
        p.reservationOwner = owner;
        out.push_back(p);

        poolIdx.erase(poolIdx.begin() + static_cast<std::ptrdiff_t>(pick)); // 1 vez por rodada
        ++made;
    }
}

} // namespace domain
} // namespace ls
