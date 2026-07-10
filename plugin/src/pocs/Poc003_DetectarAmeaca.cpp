// Living Settlements -- pocs/Poc003_DetectarAmeaca.cpp
// POC-003: deteccao de ameaca e identificacao do atacante (LEITURA).
//
// Motivacao (achado do 1o run in-game): as POCs 001/002 so olhavam
// world->player->playerCharacters e edificios itemType=BUILDING, entao
// dava p/ ver os SEUS em combate ("Atacando" key=4) mas era impossivel
// dizer QUEM atacou -- os hostis nunca eram enumerados. Alem disso o
// alarme nativo (TownAlarmState) ficou NONE durante todo o ataque, entao
// a ameaca precisa ser DERIVADA de sinais de combate, nao lida do alarme.
//
// Esta POC faz duas leituras nativas (REUSE, confirmadas nos headers reais):
//   (1) enumera personagens perto da base (GameWorld::getCharactersWithinSphere)
//       e os classifica por faccao: seus / HOSTIS / neutros;
//   (2) por personagem do jogador sob ataque, resolve a lista nativa de
//       atacantes (Character::getAllAttackers -> hand -> Character) e agrega
//       por faccao -> responde "quem me atacou". A identificacao do atacante
//       INDEPENDE do raio da varredura.
//
// Tudo LEITURA; nenhuma escrita. Identidade sempre por hand/nome, nunca por
// indice (achado do 1o run: a lista reordena).
#include "pocs/Poc003_DetectarAmeaca.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/Town.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <sstream>
#include <map>
#include <string>
#include <cstdlib> // free() -- cleanup do buffer preenchido pelo jogo

namespace ls {
namespace pocs {

namespace {

Character* firstValidPlayerCharacter(GameWorld* world) {
    if (world == 0 || world->player == 0)
        return 0;
    lektor<Character*>& chars = world->player->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        if (chars[i] != 0)
            return chars[i];
    }
    return 0;
}

std::string factionNameOf(Character* c) {
    if (c == 0)
        return "?";
    Faction* f = c->getFaction();
    if (f == 0)
        return "sem-faccao";
    return f->getName();
}

} // namespace

void poc003Run(GameWorld* world) {
    Character* anchor = firstValidPlayerCharacter(world);
    if (anchor == 0) {
        diag::log("POC-003: nenhum personagem do jogador disponivel");
        return;
    }

    // Centro/raio da base (mesmo centro FIXO da POC-002 corrigida: TownBase,
    // nao o personagem movel).
    TownBase* town = anchor->getCurrentTownLocation();
    Ogre::Vector3 center = anchor->getPosition();
    float radius = LS_POC003_RADIUS;
    if (town != 0) {
        center = town->getPosition();
        float tr = town->getRadius();
        if (tr > radius)
            radius = tr;
    }

    // --- (1) Enumerar personagens perto da base e classificar por faccao.
    // getCharactersWithinSphere tem 8 params (results,pos,far,near,always,
    // maxFar,maxNear,skip); a semantica de far/near/always NAO e documentada
    // -- passamos radius nos tres e logamos o count p/ aprender (GameWorld.h:120).
    lektor<RootObject*> nearby;
    world->getCharactersWithinSphere(nearby, center, radius, radius, radius,
                                     LS_POC003_MAX_CHARS, LS_POC003_MAX_CHARS, 0);
    int total = static_cast<int>(nearby.size());

    int meus = 0, hostis = 0, neutros = 0, semFaccao = 0;
    std::map<std::string, int> hostisPorFaccao;
    for (uint32_t i = 0; i < nearby.size(); ++i) {
        RootObject* obj = nearby[i];
        if (obj == 0)
            continue;
        // getCharactersWithinSphere devolve personagens; downcast seguro
        // dentro do contrato da API (Character : public RootObject).
        Character* c = static_cast<Character*>(obj);
        Faction* f = c->getFaction();
        if (f == 0) {
            ++semFaccao;
            continue;
        }
        if (f->isThePlayer()) {
            ++meus;
        } else if (anchor->isEnemy(c, false)) {
            ++hostis;
            hostisPorFaccao[f->getName()] += 1;
        } else {
            ++neutros;
        }
    }
    // cleanup do buffer alocado pelo jogo (lektor sem destrutor).
    if (nearby.stuff != 0) {
        free(nearby.stuff);
        nearby.stuff = 0;
        nearby.count = 0;
        nearby.maxSize = 0;
    }

    {
        std::ostringstream s;
        s << "POC-003: varredura (raio " << radius << "m): " << total
          << " personagem(ns) -- " << meus << " seus, " << hostis
          << " HOSTIS, " << neutros << " neutros, " << semFaccao << " sem-faccao";
        diag::log(s.str());
    }
    {
        int budget = LS_POC003_DETAIL_BUDGET;
        for (std::map<std::string, int>::const_iterator it = hostisPorFaccao.begin();
             it != hostisPorFaccao.end() && budget > 0; ++it, --budget) {
            std::ostringstream s;
            s << "  hostil proximo: faccao \"" << it->first << "\" x" << it->second;
            diag::log(s.str());
        }
    }

    // --- (2) Quem atacou os SEUS: por personagem do jogador com atacantes,
    // resolver a lista nativa e agregar por faccao (dedup por nome).
    lektor<Character*>& chars = world->player->playerCharacters;
    std::map<std::string, std::string> atacantes; // nome -> faccao
    int meusEmCombate = 0, meusKO = 0, meusMortos = 0, meusSobAtaque = 0;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        Character* c = chars[i];
        if (c == 0)
            continue;
        if (c->isDead()) {
            ++meusMortos;
            continue;
        }
        if (c->isUnconcious())
            ++meusKO;
        if (c->isInCombatMode(true, true))
            ++meusEmCombate;

        if (c->getAllAttackersCount() <= 0)
            continue;
        ++meusSobAtaque;

        lektor<hand> atk;
        c->getAllAttackers(atk);
        for (uint32_t k = 0; k < atk.size(); ++k) {
            hand h = atk[k];
            if (!h.isValid())
                continue;
            Character* a = h.getCharacter();
            if (a == 0)
                continue;
            atacantes[a->getName()] = factionNameOf(a);
        }
        if (atk.stuff != 0) {
            free(atk.stuff);
            atk.stuff = 0;
            atk.count = 0;
            atk.maxSize = 0;
        }
    }

    {
        std::ostringstream s;
        s << "POC-003: seus -- " << meusEmCombate << " em combate, " << meusKO
          << " KO, " << meusMortos << " mortos, " << meusSobAtaque << " sob ataque";
        diag::log(s.str());
    }

    if (atacantes.empty()) {
        diag::log("POC-003: nenhum atacante ativo neste tick (ninguem sob ataque)");
    } else {
        std::map<std::string, int> atacantesPorFaccao;
        for (std::map<std::string, std::string>::const_iterator it = atacantes.begin();
             it != atacantes.end(); ++it) {
            atacantesPorFaccao[it->second] += 1;
        }
        {
            std::ostringstream s;
            s << "POC-003: ATAQUE -- " << atacantes.size()
              << " atacante(s) distinto(s), por faccao:";
            diag::log(s.str());
        }
        int budget = LS_POC003_DETAIL_BUDGET;
        for (std::map<std::string, int>::const_iterator it = atacantesPorFaccao.begin();
             it != atacantesPorFaccao.end() && budget > 0; ++it, --budget) {
            std::ostringstream s;
            s << "  atacante faccao \"" << it->first << "\": " << it->second;
            diag::log(s.str());
        }
        budget = LS_POC003_DETAIL_BUDGET;
        for (std::map<std::string, std::string>::const_iterator it = atacantes.begin();
             it != atacantes.end() && budget > 0; ++it, --budget) {
            std::ostringstream s;
            s << "    - " << it->first << " [" << it->second << "]";
            diag::log(s.str());
        }
    }
}

} // namespace pocs
} // namespace ls
