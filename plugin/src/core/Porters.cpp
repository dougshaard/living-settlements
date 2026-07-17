// Living Settlements -- core/Porters.cpp
// Registro de carregadores declarados. ASCII-only. Main thread only.
#include "core/Porters.h"
#include "core/Diagnostics.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/util/lektor.h>

#include <cstdint>
#include <sstream>

namespace ls {
namespace core {

namespace {

static const uint32_t PORTER_MAX_CHARS = 512;

std::vector<hand>        g_porters;
std::vector<RosterEntry> g_roster;

bool sameHand(const hand& a, const hand& b) {
    // operator== de hand [V] (hand.h:43) -- identidade index+serial.
    return a == b;
}

int findPorter(const hand& h) {
    for (size_t i = 0; i < g_porters.size(); ++i) {
        if (sameHand(g_porters[i], h)) {
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
