// Living Settlements -- core/Porters.cpp
// Registro de carregadores/postos declarados + PERSISTENCIA em sidecar.
// ASCII-only. Main thread only.
//
// PERSISTENCIA (dir.15d, v0 GLOBAL por instalacao): todo clique de declarar/
// atribuir reescreve `mods\LivingSettlements\declaracoes.txt` (mesmo contrato
// de cwd do poc.txt). No boot, o arquivo vira PENDENCIAS; a cada refresh do
// roster o resolvedor casa pendencia -> personagem vivo por UID (InstanceID,
// estavel entre sessoes) e, na falta, por NOME (colisao = degrada visivel:
// resolve o primeiro; o log mostra quem). Save-agnostico: mundo arbitrario a
// frio -> o que casar, casa; o que nao casar fica pendente sem travar nada.
#include "core/Porters.h"
#include "core/Diagnostics.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/InstanceID.h>
#include <kenshi/util/lektor.h>

#include <cstdint>
#include <cstdlib>   // atof
#include <fstream>
#include <map>
#include <sstream>

namespace ls {
namespace core {

namespace {

static const uint32_t PORTER_MAX_CHARS = 512;
static const int      DECL_FILE_MAX_LINES = 512; // cap duro (arquivo lixo)
static const char* const DECL_FILE = "mods\\LivingSettlements\\declaracoes.txt";

std::vector<hand>        g_porters;
std::vector<RosterEntry> g_roster;
std::vector<PostEntry>   g_posts;
std::map<std::string, std::string> g_porterPost; // keyOf(porter) -> post key

// Pendencia vinda do arquivo: carregador declarado numa sessao anterior,
// ainda nao casado com um personagem vivo desta sessao.
struct PendingPorter {
    std::string uid;     // InstanceID::uid do char ("" = desconhecido)
    std::string name;    // nome (fallback de identidade)
    std::string postKey; // atribuicao a restaurar ("" = sem posto)
};
std::vector<PendingPorter> g_pending;
bool g_loaded = false;

// CHAVE ESTAVEL por hand::toString() (ADR-015), NAO por operator== -- o
// operator== de hand NAO casa in-game (bug real 17/07: declarar 2x so
// somava e isPorter nunca reconhecia).
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

int findPostByKey(const std::string& key) {
    for (size_t i = 0; i < g_posts.size(); ++i) {
        if (g_posts[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// UID persistente de um personagem (InstanceID e serializado no save ->
// estavel entre sessoes). Vazio se o motor nao da um.
std::string charUid(Character* c) {
    if (c == 0) {
        return std::string();
    }
    InstanceID* iid = c->getInstanceID();
    return (iid != 0) ? iid->uid : std::string();
}

std::string trim(const std::string& s) {
    std::string::size_type a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) --b;
    return s.substr(a, b - a);
}

void loadDecls(); // definida abaixo; saveDecls PRECISA dela antes de truncar

// ---- Sidecar: gravar TUDO a cada mutacao (arquivo minusculo). Formato:
//   PORTER=<uid>|<nome>            (nome = resto da linha)
//   POST=<key>|<x>|<y>|<z>|<nome>  (nome = resto da linha)
//   ASSIGN=<postKey>|<porterUidOuNome>
void saveDecls() {
    // REGRA (achado de revisao 27/07): NUNCA truncar sem ter lido -- um clique
    // no painel-do-predio antes do 1o refresh do roster (ex.: jogo pausado)
    // sobrescreveria o arquivo inteiro de sessoes anteriores com quase nada.
    loadDecls();
    std::ofstream f(DECL_FILE, std::ios::trunc);
    if (!f.is_open()) {
        diag::error("DECLARACOES: nao consegui gravar o sidecar (permissao?)");
        return;
    }
    f << "# Living Settlements -- declaracoes do jogador (carregadores/postos).\n";
    f << "# Gerado pelo mod a cada mudanca no painel; apague p/ zerar tudo.\n";
    // Carregadores vivos (identidade atual) + pendencias preservadas.
    for (size_t i = 0; i < g_porters.size(); ++i) {
        Character* c = g_porters[i].isValid() ? g_porters[i].getCharacter() : 0;
        if (c == 0) {
            continue;
        }
        f << "PORTER=" << charUid(c) << "|" << c->getName() << "\n";
        std::map<std::string, std::string>::iterator it =
            g_porterPost.find(keyOf(g_porters[i]));
        if (it != g_porterPost.end() && !it->second.empty()) {
            std::string pk = charUid(c);
            if (pk.empty()) {
                pk = c->getName();
            }
            f << "ASSIGN=" << it->second << "|" << pk << "\n";
        }
    }
    for (size_t i = 0; i < g_pending.size(); ++i) {
        f << "PORTER=" << g_pending[i].uid << "|" << g_pending[i].name << "\n";
        if (!g_pending[i].postKey.empty()) {
            std::string pk = g_pending[i].uid.empty() ? g_pending[i].name
                                                      : g_pending[i].uid;
            f << "ASSIGN=" << g_pending[i].postKey << "|" << pk << "\n";
        }
    }
    for (size_t i = 0; i < g_posts.size(); ++i) {
        f << "POST=" << g_posts[i].key << "|" << g_posts[i].x << "|"
          << g_posts[i].y << "|" << g_posts[i].z << "|" << g_posts[i].name << "\n";
    }
}

void loadDecls() {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    std::ifstream f(DECL_FILE);
    if (!f.is_open()) {
        return; // sem arquivo = sem declaracoes anteriores (fail-safe)
    }
    std::string line;
    int guard = 0;
    std::vector<std::string> assigns; // "postKey|porterKey" (aplica no fim)
    while (std::getline(f, line) && ++guard <= DECL_FILE_MAX_LINES) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        if (t.compare(0, 7, "PORTER=") == 0) {
            std::string v = t.substr(7);
            std::string::size_type p = v.find('|');
            PendingPorter pp;
            if (p == std::string::npos) {
                pp.name = v;
            } else {
                pp.uid = v.substr(0, p);
                pp.name = v.substr(p + 1);
            }
            if (!pp.uid.empty() || !pp.name.empty()) {
                g_pending.push_back(pp);
            }
        } else if (t.compare(0, 5, "POST=") == 0) {
            // key|x|y|z|nome
            std::string v = t.substr(5);
            std::string parts[4];
            int k = 0;
            std::string::size_type pos = 0;
            while (k < 4) {
                std::string::size_type e = v.find('|', pos);
                if (e == std::string::npos) {
                    break;
                }
                parts[k++] = v.substr(pos, e - pos);
                pos = e + 1;
            }
            if (k == 4 && !parts[0].empty()) {
                PostEntry p;
                p.key = parts[0];
                p.x = static_cast<float>(atof(parts[1].c_str()));
                p.y = static_cast<float>(atof(parts[2].c_str()));
                p.z = static_cast<float>(atof(parts[3].c_str()));
                p.name = v.substr(pos);
                if (findPostByKey(p.key) < 0) {
                    g_posts.push_back(p);
                }
            }
        } else if (t.compare(0, 7, "ASSIGN=") == 0) {
            assigns.push_back(t.substr(7));
        }
    }
    // Atribuicoes -> pendencias (casadas por uid-ou-nome do carregador).
    for (size_t i = 0; i < assigns.size(); ++i) {
        std::string::size_type p = assigns[i].find('|');
        if (p == std::string::npos) {
            continue;
        }
        std::string postKey = assigns[i].substr(0, p);
        std::string porterKey = assigns[i].substr(p + 1);
        for (size_t j = 0; j < g_pending.size(); ++j) {
            if ((!g_pending[j].uid.empty() && g_pending[j].uid == porterKey)
                || g_pending[j].name == porterKey) {
                g_pending[j].postKey = postKey;
                break;
            }
        }
    }
    if (!g_pending.empty() || !g_posts.empty()) {
        std::ostringstream s;
        s << "DECLARACOES: sidecar lido -- " << g_pending.size()
          << " carregador(es) e " << g_posts.size() << " posto(s) de sessoes "
          << "anteriores; resolvendo contra o roster quando o mundo subir.";
        diag::milestone(s.str());
    }
}

// Casa pendencias com o roster VIVO (uid primeiro, nome como fallback).
// Chamado a cada refresh; idempotente; pendencia nao-casada fica quieta.
void resolvePending(lektor<Character*>& chars, uint32_t n) {
    if (g_pending.empty()) {
        return;
    }
    for (size_t i = g_pending.size(); i > 0; --i) {
        PendingPorter& pp = g_pending[i - 1];
        Character* found = 0;
        for (uint32_t k = 0; k < n; ++k) {
            Character* c = chars[k];
            if (c == 0 || c->isAnimal() != 0) {
                continue;
            }
            if (!pp.uid.empty()) {
                if (charUid(c) == pp.uid) {
                    found = c;
                    break;
                }
            } else if (c->getName() == pp.name) {
                found = c;
                break;
            }
        }
        if (found == 0) {
            continue; // segue pendente (char fora do mundo/da zona)
        }
        hand h(found);
        if (findPorter(h) < 0) {
            g_porters.push_back(h);
        }
        if (!pp.postKey.empty() && findPostByKey(pp.postKey) >= 0) {
            g_porterPost[keyOf(h)] = pp.postKey;
        }
        diag::milestone("DECLARACOES: \"" + pp.name + "\" restaurado como "
                        "carregador (de sessao anterior).");
        g_pending.erase(g_pending.begin() + (i - 1));
    }
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
        g_porterPost.erase(keyOf(h));
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
    for (size_t k = 0; k < g_roster.size(); ++k) {
        if (sameHand(g_roster[k].h, h)) {
            g_roster[k].porter = (i < 0);
            break;
        }
    }
    saveDecls();
    diag::flush();
}

int porterCount() {
    return static_cast<int>(g_porters.size())
         + static_cast<int>(g_pending.size());
}

// ---- Postos ----
void declarePost(const std::string& key, const std::string& name,
                 float x, float y, float z) {
    if (key.empty()) {
        return;
    }
    loadDecls(); // mutacao pode vir antes do 1o refresh (painel-do-predio)
    int i = findPostByKey(key);
    if (i >= 0) {
        g_posts[i].name = name;
        g_posts[i].x = x; g_posts[i].y = y; g_posts[i].z = z;
        saveDecls();
        return;
    }
    PostEntry p;
    p.key = key; p.name = name; p.x = x; p.y = y; p.z = z;
    g_posts.push_back(p);
    std::ostringstream s;
    s << "POSTO: \"" << name << "\" declarado como POSTO DE CARREGADORES ("
      << g_posts.size() << " posto(s)). Carregadores atribuidos esperam aqui.";
    diag::milestone(s.str());
    saveDecls();
    diag::flush();
}

void undeclarePost(const std::string& key) {
    loadDecls();
    int i = findPostByKey(key);
    if (i < 0) {
        return;
    }
    std::string name = g_posts[i].name;
    g_posts.erase(g_posts.begin() + i);
    for (std::map<std::string, std::string>::iterator it = g_porterPost.begin();
         it != g_porterPost.end(); ) {
        if (it->second == key) {
            g_porterPost.erase(it++);
        } else {
            ++it;
        }
    }
    for (size_t j = 0; j < g_pending.size(); ++j) {
        if (g_pending[j].postKey == key) {
            g_pending[j].postKey.clear();
        }
    }
    diag::milestone("POSTO: \"" + name + "\" removido (carregadores desatribuidos).");
    saveDecls();
    diag::flush();
}

bool isPost(const std::string& key) {
    loadDecls(); // consultas da GUI podem vir antes do 1o refresh do roster
    return findPostByKey(key) >= 0;
}

int postCount() {
    loadDecls();
    return static_cast<int>(g_posts.size());
}

const std::vector<PostEntry>& posts() {
    loadDecls();
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
    int curIdx = cur.empty() ? -1 : findPostByKey(cur);
    int nextIdx = curIdx + 1;
    std::string name;
    if (nextIdx >= static_cast<int>(g_posts.size())) {
        g_porterPost.erase(pk);
        name = "(sem posto)";
    } else {
        g_porterPost[pk] = g_posts[nextIdx].key;
        name = g_posts[nextIdx].name;
    }
    std::string who;
    { Character* c = h.getCharacter(); if (c != 0) who = c->getName(); }
    diag::milestone("POSTO: \"" + who + "\" -> " + name);
    saveDecls();
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
    loadDecls(); // 1a chamada le o sidecar (lazy; barato depois)
    lektor<Character*>& chars = world->player->playerCharacters;
    uint32_t n = chars.size();
    if (n > PORTER_MAX_CHARS) {
        n = PORTER_MAX_CHARS;
    }
    resolvePending(chars, n);
    g_roster.clear();
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
    // Poda declaracoes cujo char sumiu do mundo (morte/saida). NAO poda
    // pendencias: elas representam chars de outra zona/sessao.
    for (size_t i = g_porters.size(); i > 0; --i) {
        if (!g_porters[i - 1].isValid()
            || g_porters[i - 1].getCharacter() == 0) {
            g_porterPost.erase(keyOf(g_porters[i - 1]));
            g_porters.erase(g_porters.begin() + (i - 1));
        }
    }
}

const std::vector<RosterEntry>& roster() {
    return g_roster;
}

} // namespace core
} // namespace ls
