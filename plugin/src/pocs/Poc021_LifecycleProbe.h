// Living Settlements -- pocs/Poc021_LifecycleProbe.h
// Marco 2: prova de H1/H2 (seguranca da escrita). Chamado em TODO tick
// (dentro do hook, ANTES do guard de pausa/throttle), faz EDGE-LOG dos
// sinais de ciclo de vida -- isLoadingFromASaveGame / gameResetting /
// isPaused / allThreadQueuesAreClear -- para responder: o mainLoop dispara
// durante save/load/reset? em que ordem os sinais mudam? So LEITURA de
// flags primitivos (o mais seguro que existe). Fonte: design secao 10 M2,
// hipoteses H1/H2/H_THREAD.
#ifndef LS_POCS_POC021_LIFECYCLEPROBE_H
#define LS_POCS_POC021_LIFECYCLEPROBE_H

class GameWorld;

namespace ls {
namespace pocs {

// Chamar por tick (o proprio probe faz throttle por mudanca de estado).
void poc021Probe(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC021_LIFECYCLEPROBE_H
