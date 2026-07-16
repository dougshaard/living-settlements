// Living Settlements -- pocs/Poc030_LimparCargos.h
// -----------------------------------------------------------------
// LIMPEZA TOTAL DE CARGOS (pedido do dono, 16/07): um botao no painel
// remove TODOS os permajobs do roster para o Orquestrador/Guarnicao
// recomporem a cidade do zero por necessidade e skill -- em vez do
// jogador remover cargo por cargo, personagem por personagem.
//
// Acao DESTRUTIVA e por isso: (a) so dispara pelo botao do painel
// (clearJobs e runtime-only; nao existe chave no poc.txt -- nunca
// dispara sozinha no boot); (b) toda remocao passa pelo OrderEmitter
// atras da cerca de escrita; (c) orcamento de remocoes por rodada
// (nunca desgoverna o tick) e re-tentativa automatica ate o roster
// zerar; (d) idempotente: cerca fechou no meio = continua na proxima
// rodada, sem perder nem repetir nada.
// -----------------------------------------------------------------
#ifndef LS_POCS_POC030_LIMPARCARGOS_H
#define LS_POCS_POC030_LIMPARCARGOS_H

class GameWorld;

namespace ls {
namespace pocs {

// Chamar toda rodada (main thread), ANTES do Orquestrador/Guarnicao:
// assim a mesma rodada que termina a limpeza ja recompoe a cidade.
// So age quando pocEnv().clearJobs esta armado (botao do painel);
// desarma sozinho ao concluir.
void poc030LimparCargosTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC030_LIMPARCARGOS_H
