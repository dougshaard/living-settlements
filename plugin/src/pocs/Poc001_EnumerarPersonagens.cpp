// Living Settlements — pocs/Poc001_EnumerarPersonagens.cpp
#include "pocs/Poc001_EnumerarPersonagens.h"
#include "core/Diagnostics.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/Tasker.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <sstream>

namespace ls {
namespace pocs {

void poc001Run(GameWorld* world) {
    if (world == 0 || world->player == 0) {
        diag::log("POC-001: player interface indisponivel neste tick");
        return;
    }

    lektor<Character*>& chars = world->player->playerCharacters;
    std::ostringstream head;
    head << "POC-001: " << chars.size() << " personagem(ns) do jogador";
    diag::log(head.str());

    for (uint32_t i = 0; i < chars.size(); ++i) {
        Character* c = chars[i];
        if (c == 0) {
            diag::log("POC-001: slot nulo (ignorado)");
            continue;
        }

        std::ostringstream line;
        line << "  [" << i << "] " << c->getName();

        Ogre::Vector3 pos = c->getPosition();
        line << " pos=(" << pos.x << "," << pos.y << "," << pos.z << ")";

        // --- Estado MESTRE (achado do 1o run in-game): isDead/isUnconcious
        // sao a verdade; a acao GOAP abaixo pode vir STALE durante o KO
        // (ex.: "KO acao{Atacando}"). isUnconcious e KO TRANSITORIO (a
        // maioria levanta), NAO morte -> distinguir com isDead/getProneState.
        if (c->isDead()) {
            line << " MORTO";
        } else if (c->isUnconcious()) {
            line << " KO";
        }
        switch (static_cast<int>(c->getProneState())) {
            case 2: line << " prone=ALEIJADO"; break;       // PS_CRIPPLED
            case 3: line << " prone=FINGINDO-MORTO"; break; // PS_PLAYING_DEAD
            default: break;                                 // NORMAL/STAYING_LOW/KO
        }

        // Ação corrente (leitura para POC-006): CharBody::getCurrentAction
        // devolve o Tasker ativo ou nulo. ATENCAO: pode estar STALE se o
        // personagem esta KO (achado do 1o run) -- o estado mestre acima manda.
        CharBody* body = c->getBody();
        if (body != 0) {
            if (body->isIdle())
                line << " ocioso";
            Tasker* action = body->getCurrentAction();
            if (action != 0) {
                line << " acao{key=" << static_cast<int>(action->key())
                     << " prio=" << static_cast<int>(action->priority);
                const std::string& desc = action->getDescription();
                if (!desc.empty())
                    line << " \"" << desc << "\"";
                line << "}";
            } else {
                line << " sem-acao";
            }
        } else {
            line << " sem-body";
        }

        // Gate de autoridade (POC-007 em modo leitura; ADR-017/FACT-011).
        line << (c->canTakePlayerOrdersAtThisTime()
                     ? " [aceita-ordens]"
                     : " [NAO-aceita-ordens]");

        diag::log(line.str());
    }
}

} // namespace pocs
} // namespace ls
