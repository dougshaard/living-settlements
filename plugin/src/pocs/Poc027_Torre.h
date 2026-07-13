// Living Settlements -- pocs/Poc027_Torre.h
// POC-TUR-1 (Fase A, papeis-fase2-design sec.2): provar in-game o papel
// GUARDA-DE-TORRE. Receita verificada (mapa-papeis sec.2): addJob(USE_TURRET,
// torre, TRUE, TRUE, pos); o router TROCA p/ MAN_A_TURRET_PLAYER_JOB -- a key
// 234 NA LISTA e a ASSINATURA DE SUCESSO (nao 146). E PERMAJOB_HIGHCOMBAT:
// em tempo de PAZ o guarda fora da torre NAO e falha (I-24); confirmar pela
// LISTA e observar a subida sob ataque/alerta.
//
// Dirigida por ENV-VAR (core/PocEnv.h): LS_ENABLE_POC_TUR=1 + LS_POC_WORKER=
// <nome> (+ LS_POC_TURRET=<uid> opcional; ausente = torre mais proxima).
// Default TUDO OFF. LS_POC_REVERT=1 = sessao de reversao. No maximo 1 emissao
// por sessao; escrita so via OrderEmitter atras de writeGateOpen.
//
// Lado leitura (lacunas 10/11 do mapa sec.9): enumera as torres da base com
// numOperatorsMax real e o sinal de energia (isOutOfPower).
#ifndef LS_POCS_POC027_TORRE_H
#define LS_POCS_POC027_TORRE_H

class GameWorld;

namespace ls {
namespace pocs {

void poc027TorreTick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC027_TORRE_H
