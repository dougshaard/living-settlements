// Living Settlements -- domain/TaskBoard.h
// -----------------------------------------------------------------
// Lista canonica de lacunas (Gaps) derivada do WorldSnapshot.
// Fonte: docs/design/nucleo-de-trabalho.md secoes 3.3, 5 (passo 7) e
// 5.4 (separacao de lanes -- STARVED != lacuna-de-operador).
//
// Cada Gap recebe: TaskKey deterministico (dedup entre ticks), lane
// (OPERATOR/LOGISTICS/POWER/BUILD) e servedBy (short-circuit: se um
// permajob ja mira a maquina, o GOAP nativo cuida -> nao despachamos).
// O TaskBoard NAO despacha e NAO escreve -- so classifica.
//
// Puro (sem KenshiLib). C++03. ASCII-only.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_TASKBOARD_H
#define LS_DOMAIN_TASKBOARD_H

#include "domain/WorkModel.h"
#include <vector>

namespace ls {
namespace domain {

// Constroi TODAS as lacunas do snapshot (todas as lanes), classificadas.
// Determinismo: mesma condicao -> mesmo TaskKey em todo tick.
void buildGaps(const WorldSnapshot& snap, std::vector<Gap>& out);

// Filtra so a lane OPERATOR e ainda NAO servida por permajob -- a unica
// lane que o nucleo inicial (Marco 1-3) despacha (secao 5.4).
void operatorGaps(const std::vector<Gap>& all, std::vector<Gap>& out);

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_TASKBOARD_H
