// Living Settlements -- pocs/Poc028_Guarnicao.h
// GUARNICAO: primeira fatia do reconciliador de papeis (Fase B) construida
// SO com primitivos provados in-game (POC-TUR-1/H11/Poc025). O que o jogo
// nao faz sozinho: ESCOLHER, DISTRIBUIR e REBALANCEAR guardas de torre.
//
//   - enumera as torres completas da base (classe TURRET, com uid);
//   - candidatos = personagens elegiveis SEM cargos de producao/medico
//     (guardas podem ter treino/torre -- e o pacote do papel composto,
//     diretriz 10 do dono);
//   - ranqueia pelo skill que a PROPRIA torre declara (getStatUsed);
//   - preenche 1 guarda por torre (v1 previsivel), idempotente: re-varre
//     a cada rodada e cobre torre desguarnecida (morte/remocao) sozinho.
//
// Toggle: LS_GARRISON=1 no poc.txt (default OFF). Escrita SO via
// OrderEmitter atras de writeGateOpen; cap de emissoes por rodada.
#ifndef LS_POCS_POC028_GUARNICAO_H
#define LS_POCS_POC028_GUARNICAO_H

class GameWorld;

namespace ls {
namespace pocs {

void poc028GuarnicaoTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC028_GUARNICAO_H
