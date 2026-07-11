// Living Settlements -- domain/Assignment.h
// -----------------------------------------------------------------
// Calculo do ESTADO-DESEJADO (secoes 3.3, 5 passo 8, 7.3). Casa
// workers ociosos <-> lacunas OPERATOR por skill (guloso, banda grossa),
// com as travas do gate composto que sao PURAS:
//   - sticky/dedup: nao propoe se ja ha intent ativo para a tarefa (inv. 13);
//   - RESERVA ANTES de propor (gate 5): acquireAtomic tudo-ou-nada; falhou
//     => pula a lacuna, propoe NADA;
//   - rate-limit: no maximo maxPerRound propostas por rodada (histerese);
//   - um worker so e usado uma vez por rodada (sai do pool).
//
// NAO emite escrita no jogo -- isso e do OrderEmitter (adapter), atras do
// gate de lifecycle/autoridade/versao. Em modo SOMBRA, as propostas sao
// calculadas e registradas no ledger, mas nada e emitido.
//
// Puro (sem KenshiLib). C++03. ASCII-only.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_ASSIGNMENT_H
#define LS_DOMAIN_ASSIGNMENT_H

#include "domain/WorkModel.h"
#include "domain/ReservationManager.h"
#include "domain/IntentLedger.h"
#include <vector>
#include <cstddef>

namespace ls {
namespace domain {

struct AssignmentConfig {
    int  maxPerRound;    // K: teto de propostas por rodada
    Tick leaseTTLHours;  // TTL do lease (horas-de-jogo) > periodo entre rodadas
    AssignmentConfig() : maxPerRound(4), leaseTTLHours(24.0) {}
};

struct Proposal {
    WorkerId    worker;
    TaskKey     taskKey;
    StationId   station;
    int         verb;             // WorkVerb
    std::string reservationOwner; // "task:<TaskKey>"
};

// Casa poolIdx (indices em snap.workers, CONSUMIDO -- workers usados saem)
// com opGaps. Para cada proposta bem-sucedida: adquire a reserva em rm e
// registra o Intent (PENDING) em ledger. Preenche out com as propostas.
void proposeAssignments(const WorldSnapshot& snap,
                        const std::vector<Gap>& opGaps,
                        std::vector<std::size_t>& poolIdx,
                        ReservationManager& rm,
                        IntentLedger& ledger,
                        Tick now,
                        const AssignmentConfig& cfg,
                        std::vector<Proposal>& out);

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_ASSIGNMENT_H
