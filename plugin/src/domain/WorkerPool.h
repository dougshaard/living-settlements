// Living Settlements -- domain/WorkerPool.h
// -----------------------------------------------------------------
// Conjunto de workers DESPACHAVEIS (secao 4 / 7.2). Um worker so entra
// no pool se for, ao mesmo tempo:
//   - nao-animal          (roster inclui CharacterAnimal -- excluir)
//   - ocioso              (isIdle; estabilidade por N leituras e do Debouncer)
//   - fora-de-permajob    (tem papel -> deixar ao GOAP; nao arrancar)
//   - autorizado          (gate composto: canTakeOrders && !ordem-direta
//                          && !selecionado && !incapacitado)  [invariante 7.1.3]
//   - nao-KO-de-fome      (HUNGER_KO+ nao trabalha)
//
// A aptidao por SKILL (banda) e checada no Assignment, nao aqui.
// Retorna INDICES em snap.workers (validos so no tick corrente).
//
// Puro (sem KenshiLib). C++03. ASCII-only.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_WORKERPOOL_H
#define LS_DOMAIN_WORKERPOOL_H

#include "domain/WorkModel.h"
#include <vector>
#include <cstddef>

namespace ls {
namespace domain {

// Predicado unitario (testavel isoladamente). NAO inclui proximidade
// (que depende da base) -- isso e aplicado no buildPool.
bool isDispatchable(const WorkerView& w);

// Preenche outIdx com os indices (em snap.workers) dos despachaveis.
// maxBaseDist > 0: exclui workers a mais de maxBaseDist da base (nao
// arrancar um esquadrao destacado do outro lado do mapa). <= 0 desliga
// o filtro de proximidade.
void buildPool(const WorldSnapshot& snap, double maxBaseDist,
               std::vector<std::size_t>& outIdx);

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_WORKERPOOL_H
