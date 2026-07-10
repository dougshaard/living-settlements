// Living Settlements — pocs/Poc011_EmitirOrdem.h
// POC-011 (gate técnico): "addJob/addOrder(TaskType) sob gate de
// autoridade, no hook do main-thread; observar conclusão por polling."
//
// RECORTE DESTA IMPLEMENTAÇÃO (primeiro write-path, risco mínimo):
// emite UMA ordem de movimento (Character::setDestination — a fatia
// mínima do fluxo de ordem do jogador) para UM personagem ocioso que
// aceita ordens, e observa por polling nas rodadas seguintes se ele se
// moveu. addOrder/DELIVER_RESOURCES (transporte A→B) é o passo
// seguinte — POC-005 — e só entra depois deste veredito.
//
// Segurança/autoridade (ADR-017): candidato vem de
// player->playerCharacters (facção do jogador por definição) E passa
// por canTakePlayerOrdersAtThisTime; a ordem é reversível — o jogador
// pode reordenar a qualquer momento e TP_OBEDIENCE prevalece.
//
// Identidade entre rodadas: NÃO retemos Character*; guardamos os campos
// crus do hand (index/serial/type/container) e reconstruímos o handle a
// cada observação (padrão REQ-PER-001/HYP-003).
//
// SEMÂNTICA: UM experimento por PROCESSO do jogo (o estado é estático).
// Save/load durante a janela de observação é detectado por heurísticas
// (instância de mundo + nome do alvo) e invalida/reinicia o experimento
// — o veredito só vale colhido numa sessão contínua. O veredito
// positivo exige aproximação do DESTINO pedido (não basta "se mexeu").
//
// DESLIGADA POR PADRÃO (LsConfig.h): ligar somente depois de POC-001 e
// POC-002 passarem no primeiro run em jogo.
#ifndef LS_POCS_POC011_H
#define LS_POCS_POC011_H

class GameWorld;

namespace ls {
namespace pocs {

// Chamada uma vez por rodada de POCs (TickHost, main thread, ADR-014).
// Gerencia internamente as fases: carência → emissão única → observação
// → veredito logado (POC-010).
void poc011Tick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC011_H
