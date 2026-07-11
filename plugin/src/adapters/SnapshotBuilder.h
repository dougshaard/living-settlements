// Living Settlements -- adapters/SnapshotBuilder.h
// -----------------------------------------------------------------
// UNICA fronteira de leitura com KenshiLib (design secao 4). Constroi
// um WorldSnapshot (PODs puros do dominio) a partir do estado do jogo:
//   - converte ponteiro -> ID estavel (getName / InstanceID::uid) AQUI;
//   - mapeia enums nativos -> mirrors neutros por CONSTANTE NOMEADA
//     (nunca ordinal; invariante 12);
//   - respeita threadSafe: se as filas de worker thread nao estao limpas,
//     ADIA a leitura de membros thread-mutados (currentOperators /
//     productionState) -- deixa operatorsNow vazio / prodState UNKNOWN;
//   - libera todo lektor alocado pelo jogo (free(x.stuff)).
//
// O dominio nunca ve ponteiros nem enums nativos. Chamar SOMENTE da main
// thread, e SOMENTE quando o LifecycleGate liberou a leitura.
// -----------------------------------------------------------------
#ifndef LS_ADAPTERS_SNAPSHOTBUILDER_H
#define LS_ADAPTERS_SNAPSHOTBUILDER_H

#include "domain/WorkModel.h"

class GameWorld;

namespace ls {
namespace adapters {

// Preenche out. threadSafe vem de core::threadReadsSafe(world). Retorna
// true se ancorou numa base (out.readGateOpen fica true); false se nao ha
// base/ancora (out fica vazio com readGateOpen=false).
bool buildWorkSnapshot(GameWorld* world, bool threadSafe,
                       ls::domain::WorldSnapshot& out);

} // namespace adapters
} // namespace ls

#endif // LS_ADAPTERS_SNAPSHOTBUILDER_H
