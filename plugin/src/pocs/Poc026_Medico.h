// Living Settlements -- pocs/Poc026_Medico.h
// POC-MED-1 (Fase A, papeis-fase2-design sec.2): provar in-game o papel MEDICO.
// Receita verificada (mapa-papeis sec.3): addJob(JOB_MEDIC, NULL, TRUE, TRUE, pos)
// = permajob duravel; o finder do GOAP acha os pacientes sozinho. Decisao 2 do
// dono: v1 = SO primeiros socorros (sem tala/robo/resgate).
//
// Dirigida por ENV-VAR (core/PocEnv.h): LS_ENABLE_POC_MED=1 + LS_POC_WORKER=<nome>.
// Default TUDO OFF (DLL commitada = inerte). LS_POC_REVERT=1 = sessao de reversao
// (remove o cargo em vez de criar). No maximo 1 emissao por sessao; toda escrita
// via OrderEmitter atras de writeGateOpen; degraded-safe (na duvida, nao emite).
#ifndef LS_POCS_POC026_MEDICO_H
#define LS_POCS_POC026_MEDICO_H

class GameWorld;

namespace ls {
namespace pocs {

void poc026MedicoTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC026_MEDICO_H
