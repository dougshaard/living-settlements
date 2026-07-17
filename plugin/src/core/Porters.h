// Living Settlements -- core/Porters.h
// -----------------------------------------------------------------
// REGISTRO DE CARREGADORES DECLARADOS (decisao do dono, 17/07): quem
// transporta e DECLARACAO DO JOGADOR (diretriz 11), via aba do painel
// -- nao permajob (nao existe permajob p/ 300 baus; o transporte e
// dirigido pelo mod). O Organizador ganha uma secao de pensamento
// ISOLADA: carregador declarado sai do pool de producao, da guarnicao
// e do plantao medico; o carregador (Poc029) recruta SO daqui, sem
// filtro de distancia (carregador atravessa a base).
//
// Identidade por HAND (index+serial; ADR-015) -- nome nao identifica
// (roster grande duplica nomes; bug real de 17/07). Estado de SESSAO
// (runtime-only); persistencia das declaracoes e a pergunta aberta
// dir.15d (sidecar), fora deste modulo.
//
// Threading: main thread only (GUI e POCs rodam ambas nela; ADR-014).
// A GUI so mexe NESTE estado (nenhuma escrita de jogo no clique).
// -----------------------------------------------------------------
#ifndef LS_CORE_PORTERS_H
#define LS_CORE_PORTERS_H

#include <kenshi/util/hand.h>

#include <string>
#include <vector>

class GameWorld;
class Character;

namespace ls {
namespace core {

// Uma linha do roster p/ a GUI (snapshot barato refrescado pelo tick;
// a GUI NUNCA le o mundo diretamente -- so este espelho).
struct RosterEntry {
    hand        h;
    std::string name;
    bool        porter;
    RosterEntry() : porter(false) {}
};

// O char esta declarado carregador? (comparacao por hand)
bool isPorter(Character* c);

// Alterna a declaracao (chamado pelo clique da GUI; so estado do mod).
void togglePorter(const hand& h);

int porterCount();

// Refresca o espelho do roster (tick, main thread; cap duro; ignora
// animais). Nao-op se o mundo/roster nao esta de pe.
void refreshRoster(GameWorld* world);

// Espelho corrente (ordenado como o roster do jogo).
const std::vector<RosterEntry>& roster();

} // namespace core
} // namespace ls

#endif // LS_CORE_PORTERS_H
