// Living Settlements -- pocs/Poc029_Carregador.h
// -----------------------------------------------------------------
// CARREGADOR DIRIGIDO (POC-005 fundida com a transferencia scriptada):
// o executor de transporte DO MOD. O job nativo de storage foi
// DESCARTADO por evidencia de gameplay (oscila entre baus do mesmo
// tipo, sem logica de destino, deadlock de inventario cheio ->
// inanicao); RISK-013 sanciona a alternativa: "mutacao direta so em
// transferencia scriptada consciente".
//
// Receita (toda de primitivos provados):
//   setDestination(origem) [POC-011] -> chegada -> transferencia
//   scriptada origem->carregador (removeItemDontDestroy_returnsItem ->
//   addItem; falhou = DEVOLVER) -> setDestination(destino) -> chegada
//   -> depositar. CONSERVACAO provada por contagem antes/depois nas
//   DUAS pontas de cada transferencia (o risco central e duplicar/
//   perder item).
//
// Regras de visao: REQ-LOG-001 (pull: a demanda vem do destino),
// REQ-LOG-002/003 (reserva atomica com item+qtd+capacidade+tarefa+
// trabalhador, via ReservationManager), REQ-LOG-004 (lote limitado;
// NUNCA encher o inventario). Ciclo de vida = task_lifecycle.dot
// (DISCOVERED..COMPLETED/FAILED com cooldown).
//
// SAVE-AGNOSTICO (diretriz 15): nenhum ponteiro nem estado de jogo
// cacheado entre rodadas -- so uid/nome/stringID, re-resolvidos a cada
// tick; relogio de jogo andou para tras (rollback) ou lifecycle saiu
// de OBSERVE_AND_ACT = reset frio total e re-derivacao do zero.
// -----------------------------------------------------------------
#ifndef LS_POCS_POC029_CARREGADOR_H
#define LS_POCS_POC029_CARREGADOR_H

class GameWorld;

namespace ls {
namespace pocs {

// Chamar toda rodada de POC (main thread). Age quando LS_HAUL=1 (laco
// automatico) ou quando o painel pediu 1 ciclo (haulOnce); um haul JA
// ATIVO sempre e processado ate terminar/abortar, mesmo com o toggle
// desligado no meio (nunca deixar reserva/carga pendurada).
void poc029CarregadorTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC029_CARREGADOR_H
