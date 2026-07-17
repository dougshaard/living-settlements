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

// POSTO DE CARREGADOR (decisao do dono 17/07): um PREDIO declarado onde
// carregadores esperam tarefas. Chave = uid do predio, ou "pos:x,y,z" quando
// o predio estrutural nao tem uid (achado in-game). O carregador ocioso volta
// ao posto -> fica pre-posicionado; "livre mais perto da fonte" ja escolhe o
// posto certo. E o 1o edificio ESPECIAL declarado (molde de quartel/hospital).
struct PostEntry {
    std::string key;
    std::string name;
    float       x, y, z;
    PostEntry() : x(0), y(0), z(0) {}
};

// Uma linha do roster p/ a GUI (snapshot barato refrescado pelo tick;
// a GUI NUNCA le o mundo diretamente -- so este espelho).
struct RosterEntry {
    hand        h;
    std::string name;
    bool        porter;
    std::string postKey;  // posto atribuido ("" = sem posto)
    std::string postName; // nome do posto (p/ a GUI)
    RosterEntry() : porter(false) {}
};

// O char esta declarado carregador? (comparacao por hand)
bool isPorter(Character* c);

// Alterna a declaracao (chamado pelo clique da GUI; so estado do mod).
void togglePorter(const hand& h);

int porterCount();

// ---- Postos ----
// Declara/remove um predio como posto (idempotente; chave = uid ou pos).
void declarePost(const std::string& key, const std::string& name,
                 float x, float y, float z);
void undeclarePost(const std::string& key);
bool isPost(const std::string& key);
int  postCount();
const std::vector<PostEntry>& posts();

// ---- Atribuicao carregador -> posto (manual; decisao do dono) ----
// Cicla o carregador pelos postos declarados: sem-posto -> posto1 -> ... ->
// sem-posto. Picker manual simples (poucos postos). No-op se h invalido.
void cyclePorterPost(const hand& h);
// Chave/nome do posto do carregador ("" se nenhum).
std::string porterPostKey(const hand& h);
std::string porterPostName(const hand& h);
// Posicao do posto do carregador; false se sem posto/posto sumiu.
bool porterPostPos(const hand& h, float& x, float& y, float& z);

// Refresca o espelho do roster (tick, main thread; cap duro; ignora
// animais). Nao-op se o mundo/roster nao esta de pe.
void refreshRoster(GameWorld* world);

// Espelho corrente (ordenado como o roster do jogo).
const std::vector<RosterEntry>& roster();

} // namespace core
} // namespace ls

#endif // LS_CORE_PORTERS_H
