// Living Settlements -- pocs/Poc025_Organizador.h
// Teste "cerebro organiza a base": (1) LIMPA todos os cargos de todos os chars do
// jogador (one-shot), (2) staffa os trabalhadores ociosos nos postos de PRODUCAO
// da base (idempotente). Tudo atras da cerca de escrita provada, via OrderEmitter.
// So o primitivo de producao (addJob shift=TRUE) esta provado -> so re-organiza
// producao (medico/haul/combate limpos NAO sao recriados; precisam de outros primitivos).
#ifndef LS_POCS_POC025_ORGANIZADOR_H
#define LS_POCS_POC025_ORGANIZADOR_H

class GameWorld;

namespace ls {
namespace pocs {

void poc025OrganizadorTick(GameWorld* world);

// Zera o ledger interno de atribuicoes (estacao -> quantos NOS colocamos).
// Chamado pela LIMPEZA DE CARGOS (Poc030) ao concluir: sem isto, as estacoes
// que o organizador encheu ficavam "cheias" para sempre apos o wipe
// (auditoria 27/07 -- ledger monotono + occupied=max(cur,ours)).
void poc025ResetAssignments();

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC025_ORGANIZADOR_H
