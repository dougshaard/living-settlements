// Living Settlements -- core/Porters.cpp
// Registro de carregadores declarados. ASCII-only. Main thread only.
#include "core/Porters.h"
#include "core/Diagnostics.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/util/lektor.h>

#include <cstdint>
#include <map>
#include <sstream>

namespace ls {
namespace core {

namespace {

static const uint32_t PORTER_MAX_CHARS = 512;

std::vector<hand>        g_porters;
std::vector<RosterEntry> g_roster;
std::vector<PostEntry>   g_posts;
std::map<std::string, std::string> g_porterPost; // keyOf(porter) -> post key

int findPostByKey(const std::string& key) {
    for (size_t i = 0; i < g_posts.size(); ++i) {
        if (g_posts[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// CHAVE ESTAVEL por hand::toString() (ADR-015), NAO por operator==. O
// operator== de hand mostrou-se NAO-confiavel in-game (17/07): declarar o
// mesmo char duas vezes nunca casava -> so somava, nunca alternava, e
// isPorter() nunca reconhecia os declarados (carregador via "16 declarados"
// mas "nenhum disponivel"). toString() serializa index+serial+type -- a
// identidade de referencia, estavel entre chamadas na mesma sessao.
std::string keyOf(const hand& h) {
    return h.toString();
}

bool sameHand(const hand& a, const hand& b) {
    return keyOf(a) == keyOf(b);
}

int findPorter(const hand& h) {
    std::string k = keyOf(h);
    for (size_t i = 0; i < g_porters.size(); ++i) {
        if (keyOf(g_porters[i]) == k) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

bool isPorter(Character* c) {
    if (c == 0 || g_porters.empty()) {
        return false;
    }
    hand h(c);
    return findPorter(h) >= 0;
}

void togglePorter(const hand& h) {
    if (!h.isValid()) {
        return;
    }
    int i = findPorter(h);
    std::string name;
    {
        Character* c = h.getCharacter();
        if (c != 0) {
            name = c->getName();
        }
    }
    if (i >= 0) {
        g_porters.erase(g_porters.begin() + i);
        std::ostringstream s;
        s << "CARREGADORES: \"" << name << "\" REMOVIDO da declaracao ("
          << g_porters.size() << " declarados)";
        diag::milestone(s.str());
    } else {
        g_porters.push_back(h);
        std::ostringstream s;
        s << "CARREGADORES: \"" << name << "\" DECLARADO carregador ("
          << g_porters.size() << " declarados) -- fora da producao/guarnicao/"
          << "plantao; o transporte recruta daqui.";
        diag::milestone(s.str());
    }
    // Espelha o flag no roster (a GUI le daqui).
    for (size_t k = 0; k < g_roster.size(); ++k) {
        if (sameHand(g_roster[k].h, h)) {
            g_roster[k].porter = (i < 0);
            break;
        }
    }
    diag::flush();
}

int porterCount() {
    return static_cast<int>(g_porters.size());
}

// ---- Postos ----
void declarePost(const std::string& key, const std::string& name,
                 float x, float y, float z) {
    if (key.empty()) {
        return;
    }
    int i = findPostByKey(key);
    if (i >= 0) {
        g_posts[i].name = name; // atualiza rotulo/pos
        g_posts[i].x = x; g_posts[i].y = y; g_posts[i].z = z;
        return;
    }
    PostEntry p;
    p.key = key; p.name = name; p.x = x; p.y = y; p.z = z;
    g_posts.push_back(p);
    std::ostringstream s;
    s << "POSTO: \"" << name << "\" declarado como POSTO DE CARREGADORES ("
      << g_posts.size() << " posto(s)). Carregadores atribuidos esperam aqui.";
    diag::milestone(s.str());
    diag::flush();
}

void undeclarePost(const std::string& key) {
    int i = findPostByKey(key);
    if (i < 0) {
        return;
    }
    std::string name = g_posts[i].name;
    g_posts.erase(g_posts.begin() + i);
    // Desatribui quem apontava p/ este posto.
    for (std::map<std::string, std::string>::iterator it = g_porterPost.begin();
         it != g_porterPost.end(); ) {
        if (it->second == key) {
            g_porterPost.erase(it++);
        } else {
            ++it;
        }
    }
    diag::milestone("POSTO: \"" + name + "\" removido (carregadores desatribuidos).");
    diag::flush();
}

bool isPost(const std::string& key) {
    return findPostByKey(key) >= 0;
}

int postCount() {
    return static_cast<int>(g_posts.size());
}

const std::vector<PostEntry>& posts() {
    return g_posts;
}

// ---- Atribuicao carregador -> posto ----
void cyclePorterPost(const hand& h) {
    if (!h.isValid()) {
        return;
    }
    std::string pk = keyOf(h);
    std::string cur;
    std::map<std::string, std::string>::iterator it = g_porterPost.find(pk);
    if (it != g_porterPost.end()) {
        cur = it->second;
    }
    // Proximo posto na ordem; do ultimo volta a "sem posto".
    int curIdx = cur.empty() ? -1 : findPostByKey(cur);
    int nextIdx = curIdx + 1;
    std::string name;
    if (nextIdx >= static_cast<int>(g_posts.size())) {
        g_porterPost.erase(pk); // volta a sem-posto
        name = "(sem posto)";
    } else {
        g_porterPost[pk] = g_posts[nextIdx].key;
        name = g_posts[nextIdx].name;
    }
    std::string who;
    { Character* c = h.getCharacter(); if (c != 0) who = c->getName(); }
    diag::milestone("POSTO: \"" + who + "\" -> " + name);
    diag::flush();
}

std::string porterPostKey(const hand& h) {
    std::map<std::string, std::string>::iterator it = g_porterPost.find(keyOf(h));
    return (it == g_porterPost.end()) ? std::string() : it->second;
}

std::string porterPostName(const hand& h) {
    std::string k = porterPostKey(h);
    if (k.empty()) {
        return std::string();
    }
    int i = findPostByKey(k);
    return (i < 0) ? std::string() : g_posts[i].name;
}

bool porterPostPos(const hand& h, float& x, float& y, float& z) {
    std::string k = porterPostKey(h);
    if (k.empty()) {
        return false;
    }
    int i = findPostByKey(k);
    if (i < 0) {
        return false;
    }
    x = g_posts[i].x; y = g_posts[i].y; z = g_posts[i].z;
    return true;
}

void refreshRoster(GameWorld* world) {
    if (world == 0 || world->player == 0) {
        return;
    }
    g_roster.clear();
    lektor<Character*>& chars = world->player->playerCharacters;
    uint32_t n = chars.size();
    if (n > PORTER_MAX_CHARS) {
        n = PORTER_MAX_CHARS;
    }
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c == 0 || c->isAnimal() != 0) {
            continue;
        }
        RosterEntry e;
        e.h = hand(c);
        e.name = c->getName();
        e.porter = (findPorter(e.h) >= 0);
        e.postKey = porterPostKey(e.h);
        e.postName = porterPostName(e.h);
        g_roster.push_back(e);
    }
    // Poda declaracoes cujo char sumiu do mundo (morte/saida): hand
    // invalido nao volta (o jogo recicla serial) -- degraded-safe.
    for (size_t i = g_porters.size(); i > 0; --i) {
        if (!g_porters[i - 1].isValid()
            || g_porters[i - 1].getCharacter() == 0) {
            g_porters.erase(g_porters.begin() + (i - 1));
        }
    }
}

const std::vector<RosterEntry>& roster() {
    return g_roster;
}

} // namespace core
} // namespace ls
