// Living Settlements — pocs/Poc001_EnumerarPersonagens.h
// POC-001 (obrigatório): "Listar personagens do jogador e seus estados
// relevantes sem crash." Também exercita, em modo leitura, os insumos de
// POC-006 (ação corrente via CharBody/Tasker) e POC-007 (autoridade:
// canTakePlayerOrdersAtThisTime — FACT-011).
#ifndef LS_POCS_POC001_H
#define LS_POCS_POC001_H

class GameWorld;

namespace ls {
namespace pocs {

// Percorre player->playerCharacters e registra nome, posição, estado
// (inconsciente/ocioso), ação corrente (TaskType + descrição) e o gate
// de autoridade. Chamado pelo TickHost na main thread (ADR-014).
void poc001Run(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC001_H
