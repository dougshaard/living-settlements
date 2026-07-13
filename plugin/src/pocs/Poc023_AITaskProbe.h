// Living Settlements -- pocs/Poc023_AITaskProbe.h
// Read-only: despeja o sistema de tarefas da IA (jobs/permajobs) do personagem
// selecionado -> revela o TaskType REAL que o painel de jobs do Kenshi usa
// (resolve o misterio do H11: addJob caiu em `jobs`, nao `permajobs`). ZERO escrita.
#ifndef LS_POCS_POC023_AITASKPROBE_H
#define LS_POCS_POC023_AITASKPROBE_H

class GameWorld;

namespace ls {
namespace pocs {

void poc023AiProbeTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC023_AITASKPROBE_H
