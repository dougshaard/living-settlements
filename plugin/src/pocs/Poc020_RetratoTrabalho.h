// Living Settlements -- pocs/Poc020_RetratoTrabalho.h
// Marco 0: POC de leitura (OBSERVE_ONLY) do nucleo de trabalho.
// Gate de leitura fail-closed -> SnapshotBuilder -> dominio em SOMBRA
// (reserva 5.1, debounce, ledger, assignment) -> retrato + painel Por-Que.
// ZERO escrita. Fonte: design secao 10, Marco 0.
#ifndef LS_POCS_POC020_RETRATOTRABALHO_H
#define LS_POCS_POC020_RETRATOTRABALHO_H

class GameWorld;

namespace ls {
namespace pocs {

void poc020Run(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC020_RETRATOTRABALHO_H
