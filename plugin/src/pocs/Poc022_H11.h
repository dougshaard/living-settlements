// Living Settlements -- pocs/Poc022_H11.h
// P5-3: PRIMEIRA ESCRITA controlada. Prova H11 -- addJob(getDefaultTask(), mina)
// cria um permajob DURAVEL? Escopo minusculo, reversivel, single-shot, atras da
// cerca de escrita provada. Switch proprio: LS_ENABLE_H11 (LS_M0_WRITES_ENABLED
// segue false). Fonte: docs/design/sessao-2026-07-11-achados.md sec.16.4.
#ifndef LS_POCS_POC022_H11_H
#define LS_POCS_POC022_H11_H

class GameWorld;

namespace ls {
namespace pocs {

void poc022H11Tick(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC022_H11_H
