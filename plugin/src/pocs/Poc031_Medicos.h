// Living Settlements -- pocs/Poc031_Medicos.h
// -----------------------------------------------------------------
// MEDICOS COMO PAPEL RECONCILIADO (pedido do dono, 17/07: "76 feridos
// no chao e nao estamos fazendo nada"). A POC-026 e observacao
// single-shot -- quando a limpeza de cargos apagou o JOB_MEDIC, ninguem
// repos e o roster sangrou sem resposta. Este modulo e o reconciliador
// do papel, no padrao da Guarnicao (Poc028): toda rodada garante N
// medicos vivos e equipados, repondo sozinho o que morte/limpeza/
// rollback desfizer.
//
// Regras:
// - alvo = 1 + feridos/25 (teto MEDIC_TARGET_CAP); ferido = score de
//   primeiros socorros pre-calculado pelo jogo (> 0, inv.21);
// - candidato: kit VALIDO primeiro (sem ITEM_FIRSTAID o cargo nao
//   executa, I-25), depois livre-de-cargos, depois skill STAT_MEDIC;
//   guardas de torre ficam de fora (guarda guarda -- diretriz 10);
// - PRIORIDADE (mapa-papeis sec.8.5, a FALHA do "arrastar Curar ao
//   topo"): addJob apenda no FIM = prioridade minima. Char livre
//   recebe o cargo direto (vira topo natural). Char com cargos e
//   RECONSTRUIDO: captura os cargos (todos de tipos conhecidos, senao
//   pula o char), remove todos, emite JOB_MEDIC primeiro e re-emite os
//   antigos na mesma ordem -- Curar no topo, producao embaixo, e o
//   GOAP produz quando nao ha feridos. 1 reconstrucao por rodada.
// - toda escrita via OrderEmitter atras da cerca; caps duros; nunca
//   remove medicos excedentes (aditivo; sem ferido o cargo cede).
// -----------------------------------------------------------------
#ifndef LS_POCS_POC031_MEDICOS_H
#define LS_POCS_POC031_MEDICOS_H

class GameWorld;

namespace ls {
namespace pocs {

// Chamar toda rodada (main thread), ANTES do Orquestrador: o papel de
// medico reivindica os melhores candidatos antes da producao consumir
// os livres. Age quando pocEnv().medicRole esta ligado.
void poc031MedicosTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC031_MEDICOS_H
