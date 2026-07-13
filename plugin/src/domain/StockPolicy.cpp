// Living Settlements -- domain/StockPolicy.cpp
// Puro, C++03, ASCII-only.
#include "domain/StockPolicy.h"

#include <set>

namespace ls {
namespace domain {

namespace {

// Tier deste ciclo. Primario = sinais nativos (getResourcesNeededBecause*):
// Empty subset NotFull da a banda morta INTRINSECA (sem cutoff inventado).
// Fallback parametrico (fill) so quando nao ha need-list, com clamp de banda.
int rawTierOf(const ResourceVerdict& v, const StockConfig& cfg) {
    if (v.criticalSinks > 0 || v.topupSinks > 0) {
        if (v.criticalSinks >= cfg.critSinksForHungry) return STK_CRITICAL;
        if (v.topupSinks == 0) return STK_SATED;
        return STK_LOW;
    }
    if (v.aggCap > 0.0) {
        double fmin = cfg.fillMin;
        double ftgt = cfg.fillTarget;
        if (ftgt - fmin < cfg.bandMinGap) {       // clamp: banda nunca estreita
            double mid = (fmin + ftgt) / 2.0;
            fmin = mid - cfg.bandMinGap / 2.0;
            ftgt = mid + cfg.bandMinGap / 2.0;
        }
        if (v.fillRatio <= fmin) return STK_CRITICAL;
        if (v.fillRatio >= ftgt) return STK_SATED;
        return STK_LOW;
    }
    return STK_UNKNOWN;
}

int effSkill(const WorkerView& w, int stat) {
    if (stat < 0) return 0;
    int l = w.skillFor(stat);
    return (l < 0) ? 0 : l;
}

} // namespace

void StockPolicy::evaluate(const WorldSnapshot& snap,
                           const std::vector<std::size_t>& pool,
                           const StockConfig& cfg, StockReport& out) {
    out.resources.clear();
    out.wouldStaff.clear();
    out.hungryResources = 0;
    out.weightExcluded = 0;
    if (!snap.readGateOpen) {
        return;
    }

    // (1) Agrega por recurso -- so estacoes OBSERVADAS (inv.21).
    std::map<std::string, ResourceVerdict> agg;
    for (std::size_t i = 0; i < snap.stations.size(); ++i) {
        const StationView& s = snap.stations[i];
        if (!s.prodObserved) {
            continue;
        }
        if (!s.producesItemKey.empty()) {
            ResourceVerdict& v = agg[s.producesItemKey];
            v.itemKey = s.producesItemKey;
            if (v.itemName.empty()) {
                v.itemName = s.producesItemName;
            }
            ++v.producers;
        }
        for (std::size_t j = 0; j < s.needsCritical.size(); ++j) {
            ResourceVerdict& v = agg[s.needsCritical[j].itemKey];
            v.itemKey = s.needsCritical[j].itemKey;
            ++v.criticalSinks;
        }
        for (std::size_t j = 0; j < s.needsTopup.size(); ++j) {
            ResourceVerdict& v = agg[s.needsTopup[j].itemKey];
            v.itemKey = s.needsTopup[j].itemKey;
            ++v.topupSinks;
        }
        for (std::size_t j = 0; j < s.inputs.size(); ++j) {
            const StockSlotView& in = s.inputs[j];
            if (in.itemKey.empty()) {
                continue;
            }
            ResourceVerdict& v = agg[in.itemKey];
            v.itemKey = in.itemKey;
            v.aggAmount += in.amount;
            v.aggCap += in.maxCapacity;
        }
    }

    // (2) Tier + debounce + subida-observada.
    for (std::map<std::string, ResourceVerdict>::iterator it = agg.begin();
         it != agg.end(); ++it) {
        ResourceVerdict& v = it->second;
        v.fillRatio = (v.aggCap > 0.0) ? (v.aggAmount / v.aggCap) : 0.0;
        // Estoque REAL da base (STORAGE), nao o buffer de input da maquina.
        // Achado de calibracao 2026-07-11: ConsumptionItem.amount fica ~0 mesmo
        // em recurso bem-suprido -> a subida-observada precisa vir DAQUI (sec.4.4).
        // "MEDIDO" = ha uma leitura de estoque deste recurso NESTE tick: a chave
        // esta no mapa (o adapter semeia todo item produzido, mesmo com 0). Um
        // recurso ausente (so consumido/importado, ou storage sumiu da varredura)
        // NAO e 0 duro -> e "sem sinal": rise=0 e a referencia nao se move.
        bool measured = false;
        if (snap.baseStockObserved) {
            std::map<std::string, double>::const_iterator bs =
                snap.baseStock.find(v.itemKey);
            if (bs != snap.baseStock.end()) {
                v.baseStock = bs->second;
                measured = true;
            }
        }
        v.rawTier = rawTierOf(v, cfg);
        Mem& m = mem_[v.itemKey];
        if (v.rawTier == m.lastRawTier) {
            ++m.consec;
        } else {
            m.lastRawTier = v.rawTier;
            m.consec = 1;
        }
        if (m.consec >= cfg.demandConsistentTicks) {
            m.stableTier = v.rawTier;
        }
        v.tier = m.stableTier;
        v.stable = (m.consec >= cfg.demandConsistentTicks);
        // Subida-observada = delta desde a ULTIMA MEDICAO. A 1a medicao (apos
        // reset/load ou 1a vez) NAO e um delta -> rise=0 e so semeia a baseline
        // (senao reportaria +estoque-inteiro por leitura, poluindo a calibracao).
        // Sem medicao neste tick: sem sinal (rise=0, referencia intacta).
        if (measured) {
            if (m.stockSeeded) {
                v.riseSinceLast = v.baseStock - m.lastBaseStock;
            } else {
                v.riseSinceLast = 0.0;
                m.stockSeeded = true;
            }
            m.lastBaseStock = v.baseStock;
        } else {
            v.riseSinceLast = 0.0;
        }
        if (v.tier == STK_CRITICAL) {
            ++out.hungryResources;
        }
        out.resources.push_back(v);
    }

    // Ordena por criticalSinks desc, depois topupSinks desc, depois itemKey asc
    // (determinismo). Insertion sort -- vetor pequeno.
    for (std::size_t a = 1; a < out.resources.size(); ++a) {
        ResourceVerdict key = out.resources[a];
        std::size_t b = a;
        while (b > 0) {
            const ResourceVerdict& prev = out.resources[b - 1];
            bool keyBigger;
            if (key.criticalSinks != prev.criticalSinks) {
                keyBigger = (key.criticalSinks > prev.criticalSinks);
            } else if (key.topupSinks != prev.topupSinks) {
                keyBigger = (key.topupSinks > prev.topupSinks);
            } else {
                keyBigger = (key.itemKey < prev.itemKey);
            }
            if (!keyBigger) {
                break;
            }
            out.resources[b] = out.resources[b - 1];
            --b;
        }
        out.resources[b] = key;
    }

    // Candidatos pesados no pool (inv.17) -- contados uma vez (unicos). So conta
    // quem foi OBSERVADO carregado (rodada thread-safe): peso nao observado nao
    // exclui ninguem (evita "excluido por peso 0" numa rodada thread-adiada).
    for (std::size_t p = 0; p < pool.size(); ++p) {
        if (snap.workers[pool[p]].observedHeavy(cfg.staffMaxLoadRatio)) {
            ++out.weightExcluded;
        }
    }

    // Conjunto de maquinas ja servidas por permajob (F8) -- O(1) por consulta.
    std::set<StationId> served;
    for (std::size_t i = 0; i < snap.workers.size(); ++i) {
        const WorkerView& w = snap.workers[i];
        for (std::size_t j = 0; j < w.permajobs.size(); ++j) {
            if (!w.permajobs[j].roleMachineId.empty()) {
                served.insert(w.permajobs[j].roleMachineId);
            }
        }
    }

    // (3) Would-staff: p/ cada recurso HUNGRY (debounced), acha minas produtoras
    // staffaveis e o melhor worker APTO por peso. Rate-limited (log).
    int made = 0;
    for (std::size_t r = 0;
         r < out.resources.size() && made < cfg.maxWouldStaffPerRound; ++r) {
        const ResourceVerdict& v = out.resources[r];
        if (v.tier != STK_CRITICAL) {
            continue; // so os HUNGRY debounced
        }
        for (std::size_t i = 0;
             i < snap.stations.size() && made < cfg.maxWouldStaffPerRound; ++i) {
            const StationView& s = snap.stations[i];
            if (s.producesItemKey != v.itemKey) {
                continue;
            }
            // Mina staffavel: observada, NORMAL, vaga livre, precisa operar,
            // nao dispensada, producao -- e nao ja servida por permajob (F8).
            if (!(s.prodObserved && s.operatorsObserved
                  && s.productionState == PROD_NORMAL
                  && s.needsOperating && s.hasFreeSlot() && !s.dontNeedWork
                  && s.workClass == WC_PRODUCTION)) {
                continue;
            }
            if (served.find(s.id) != served.end()) {
                continue; // GOAP ja cuida (papel = permajob)
            }

            // Melhor worker do pool, apto por peso (skill desc, dist asc, id asc).
            std::size_t best = pool.size();
            int bestSkill = -1;
            double bestDist = 0.0;
            for (std::size_t p = 0; p < pool.size(); ++p) {
                const WorkerView& w = snap.workers[pool[p]];
                if (w.observedHeavy(cfg.staffMaxLoadRatio)) {
                    continue; // observado pesado -> nunca despachar (inv.17)
                }
                int sk = effSkill(w, s.statUsed);
                double d2 = distanceSquared(w.posX, w.posY, w.posZ,
                                            s.posX, s.posY, s.posZ);
                bool better;
                if (best >= pool.size()) {
                    better = true;
                } else if (sk != bestSkill) {
                    better = (sk > bestSkill);
                } else if (d2 != bestDist) {
                    better = (d2 < bestDist);
                } else {
                    better = (w.id < snap.workers[pool[best]].id);
                }
                if (better) {
                    best = p;
                    bestSkill = sk;
                    bestDist = d2;
                }
            }

            StaffIntentShadow si;
            si.itemKey = v.itemKey;
            si.mineId = s.id;
            si.verb = (s.defaultVerb != WV_UNKNOWN)
                      ? s.defaultVerb : WV_OPERATE_MACHINERY;
            if (best < pool.size()) {
                si.workerId = snap.workers[pool[best]].id;
                si.reason = "HUNGRY: staffaria (candidato apto)";
            } else {
                si.reason = "HUNGRY: staffavel, SEM candidato apto (pool vazio/pesado)";
            }
            out.wouldStaff.push_back(si);
            ++made;
        }
    }
}

const char* stockTierName(int tier) {
    switch (tier) {
        case STK_CRITICAL: return "HUNGRY";
        case STK_LOW:      return "BAND";
        case STK_SATED:    return "SATED";
        default:           return "?";
    }
}

} // namespace domain
} // namespace ls
