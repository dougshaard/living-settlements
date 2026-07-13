// Living Settlements -- domain/StockPolicy.h
// -----------------------------------------------------------------
// Parte 5 (P5-0, SOMBRA): agrega o estado de estoque por RECURSO, aplica a
// banda morta nativa (Empty subset NotFull) com debounce, mede a subida
// OBSERVADA do estoque (dinamica real do jogo), e computa quais staffings de
// mina TOMARIA (com gates de peso e servedBy) -- SEM emitir nada.
//
// Fronteira: o anti-oscilacao de ACOES (dwell/breaker/watchdog, design secao
// 4.2-4.6) e malha-FECHADA e so exercita quando a camada AGE -> fica para a
// fase de escrita (P5-3). Aqui provamos a LOGICA de demanda (banda morta +
// debounce) e colhemos o sinal de calibracao (o pulsar do estoque).
//
// Puro (sem KenshiLib). C++03. ASCII-only. Fonte: docs/design/parte5-*.md.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_STOCKPOLICY_H
#define LS_DOMAIN_STOCKPOLICY_H

#include "domain/WorkModel.h"
#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace ls {
namespace domain {

enum StockTier {
    STK_UNKNOWN = 0,
    STK_CRITICAL,  // HUNGRY: >= MIN sinks com R em needsCritical (< MIN)
    STK_LOW,       // BAND (zona morta): R em needsTopup, nao critico
    STK_SATED      // SATISFIED: nenhum sink quer R (topupSinks == 0)
};

// Veredito de um recurso neste ciclo (+ estado debounced).
struct ResourceVerdict {
    std::string itemKey;
    std::string itemName;
    int    tier;            // StockTier debounced (STK_UNKNOWN ate estabilizar)
    int    rawTier;         // tier deste ciclo (antes do debounce)
    int    criticalSinks;   // sinks com R em getResourcesNeededBecauseEmpty
    int    topupSinks;      // sinks com R em getResourcesNeededBecauseNotFull
    int    producers;       // estacoes que produzem R (observadas)
    double aggAmount;       // soma dos inputs.amount de R (buffer de input; ~0)
    double aggCap;          // soma dos inputs.maxCapacity de R (buffer de input)
    double fillRatio;       // aggAmount/aggCap (0 se cap 0) -- SECUNDARIO (buffer)
    double baseStock;       // estoque REAL da base (STORAGE), snap.baseStock[R]
    double riseSinceLast;   // subida OBSERVADA do estoque de STORAGE desde a
                            // ultima leitura observada (0 em rodada nao-observada)
    bool   stable;          // tier estavel por N leituras (debounce satisfeito)
    ResourceVerdict() : tier(STK_UNKNOWN), rawTier(STK_UNKNOWN),
        criticalSinks(0), topupSinks(0), producers(0), aggAmount(0.0),
        aggCap(0.0), fillRatio(0.0), baseStock(0.0), riseSinceLast(0.0),
        stable(false) {}
};

// Uma decisao de staffing que a camada TOMARIA (em SOMBRA: nunca emitida).
struct StaffIntentShadow {
    std::string itemKey;    // recurso HUNGRY que motivou
    StationId   mineId;     // mina produtora candidata
    WorkerId    workerId;   // melhor candidato apto ("" se nenhum)
    int         verb;       // defaultVerb da mina (getDefaultTask)
    std::string reason;     // por que (ou por que sem candidato)
    StaffIntentShadow() : verb(WV_UNKNOWN) {}
};

struct StockConfig {
    int    critSinksForHungry;   // MIN de sinks criticos p/ marcar HUNGRY
    double fillMin, fillTarget;  // banda morta FALLBACK (quando sem need-list)
    double bandMinGap;           // clamp: ALVO-MIN nunca menor que isto
    int    demandConsistentTicks;// debounce (leituras consistentes)
    double staffMaxLoadRatio;    // nao despachar quem ja carrega >= isto (inv.17)
    double handsEmptyEps;        // tolerancia p/ "maos vazias" (inv.17)
    int    maxWouldStaffPerRound;// rate-limit do log de decisoes
    StockConfig() : critSinksForHungry(1), fillMin(0.15), fillTarget(0.85),
        bandMinGap(0.40), demandConsistentTicks(3), staffMaxLoadRatio(0.50),
        handsEmptyEps(0.1), maxWouldStaffPerRound(8) {}
};

struct StockReport {
    std::vector<ResourceVerdict>   resources;   // ordenado: criticalSinks desc
    std::vector<StaffIntentShadow> wouldStaff;  // decisoes (rate-limited)
    int hungryResources;    // quantos recursos HUNGRY (debounced)
    int weightExcluded;     // candidatos do pool pulados por peso (unicos)
    StockReport() : hungryResources(0), weightExcluded(0) {}
};

// Estado cross-tick: debounce do tier + ultimo estoque de STORAGE por recurso
// (subida observada). Reset TOTAL no load (design 7.5, inv.4/7).
class StockPolicy {
public:
    StockPolicy() {}
    // Roda um ciclo em SOMBRA. pool = indices de workers despachaveis (buildPool).
    // Nao muta o mundo; nao adquire reserva; nao emite.
    void evaluate(const WorldSnapshot& snap, const std::vector<std::size_t>& pool,
                  const StockConfig& cfg, StockReport& out);
    void reset() { mem_.clear(); }

private:
    struct Mem {
        int    lastRawTier;
        int    stableTier;
        int    consec;
        double lastBaseStock;   // estoque de STORAGE na ultima leitura MEDIDA
        bool   stockSeeded;     // ja houve uma 1a medicao (evita rise espurio)
        Mem() : lastRawTier(STK_UNKNOWN), stableTier(STK_UNKNOWN),
                consec(0), lastBaseStock(0.0), stockSeeded(false) {}
    };
    std::map<std::string, Mem> mem_;
};

const char* stockTierName(int tier);

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_STOCKPOLICY_H
