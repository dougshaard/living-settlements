// Living Settlements -- pocs/Poc003_DetectarAmeaca.h
// POC-003: deteccao de ameaca / identificacao do atacante (LEITURA).
// Fecha o gap do 1o run: as POCs so olhavam os personagens do jogador,
// entao dava p/ ver combate ("Atacando") mas nunca QUEM atacou.
#ifndef LS_POCS_POC003_H
#define LS_POCS_POC003_H

class GameWorld;

namespace ls {
namespace pocs {

// Uma passada por rodada de POCs (TickHost, main thread, ADR-014).
void poc003Run(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC003_H
