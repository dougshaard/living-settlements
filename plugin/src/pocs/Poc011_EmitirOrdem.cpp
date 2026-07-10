// Living Settlements — pocs/Poc011_EmitirOrdem.cpp
#include "pocs/Poc011_EmitirOrdem.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/Tasker.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Identidade do alvo entre rodadas: campos crus do hand (POD, zero-init
// estático seguro — nenhum construtor do jogo roda no load da DLL).
struct TargetId {
    unsigned int index;
    unsigned int serial;
    unsigned int container;
    unsigned int containerSerial;
    int type; // itemType
    bool set;
};

enum Phase { PHASE_GRACE, PHASE_OBSERVE, PHASE_DONE };

Phase g_phase = PHASE_GRACE;
int g_graceLeft = LS_POC011_GRACE_ROUNDS;
int g_observeLeft = LS_POC011_OBSERVE_ROUNDS;
TargetId g_target = { 0, 0, 0, 0, 0, false };
float g_startX = 0.0f, g_startY = 0.0f, g_startZ = 0.0f;
// Destino pedido (achado da revisao: sem ele o veredito positivo seria
// "moveu por qualquer motivo", nao "moveu para onde mandamos").
float g_destX = 0.0f, g_destY = 0.0f, g_destZ = 0.0f;
// Identidade do mundo na emissao (ponteiro usado SO para comparacao,
// nunca dereferenciado depois) + nome do alvo: detectores baratos de
// save/load no meio da janela de observacao. Imperfeitos (GameWorld
// pode ser reutilizado entre loads), mas cortam a maioria dos vereditos
// falsos; a semantica real da POC e "uma vez por PROCESSO" e um
// save/load durante a janela invalida o veredito (documentado).
const void* g_worldAtEmit = 0;
std::string g_targetName;

void resetExperiment(const char* reason) {
    std::ostringstream line;
    line << "POC-011 INCONCLUSIVO: " << reason
         << " -- reiniciando o experimento (nova carencia)";
    diag::milestone(line.str());
    g_phase = PHASE_GRACE;
    g_graceLeft = LS_POC011_GRACE_ROUNDS;
    g_observeLeft = LS_POC011_OBSERVE_ROUNDS;
    g_target.set = false;
    g_worldAtEmit = 0;
    g_targetName.clear();
}

Character* resolveTarget() {
    if (!g_target.set)
        return 0;
    // Reconstrução do handle a partir dos campos crus (HYP-003):
    // hand(index, serial, type, container, containerSerial).
    hand h(g_target.index, g_target.serial,
           static_cast<itemType>(g_target.type),
           g_target.container, g_target.containerSerial);
    if (!h.isValid())
        return 0;
    return h.getCharacter();
}

void tryEmit(GameWorld* world) {
    if (world->player == 0) {
        diag::log("POC-011: player interface indisponivel; aguardando");
        return;
    }

    lektor<Character*>& chars = world->player->playerCharacters;
    // Itera do FIM para o começo (achado da revisao): o primeiro slot
    // tende a ser o personagem principal/selecionado; preferimos o de
    // menor risco de estar posicionado deliberadamente pelo jogador.
    for (uint32_t k = chars.size(); k > 0; --k) {
        Character* c = chars[k - 1];
        if (c == 0)
            continue;
        CharBody* body = c->getBody();
        // Critérios de segurança: ocioso, consciente e aceitando ordens
        // (ADR-017). Só UM personagem, só UMA vez por processo.
        if (body == 0 || !body->isIdle())
            continue;
        if (c->isUnconcious())
            continue;
        if (!c->canTakePlayerOrdersAtThisTime())
            continue;
        // Nao mexer no personagem atualmente SELECIONADO pelo jogador
        // (hand::operator==(const RootObjectBase*) — verificado em hand.h).
        if (world->player->selectedCharacter == c)
            continue;

        // Registra a identidade por campos crus do handle (REQ-PER-001).
        const hand& h = c->getHandle();
        g_target.index = h.index;
        g_target.serial = h.serial;
        g_target.container = h.container;
        g_target.containerSerial = h.containerSerial;
        g_target.type = static_cast<int>(h.type);
        g_target.set = true;

        Ogre::Vector3 start = c->getPosition();
        g_startX = start.x;
        g_startY = start.y;
        g_startZ = start.z;

        Ogre::Vector3 dest(start.x + LS_POC011_OFFSET_METERS, start.y, start.z);
        g_destX = dest.x;
        g_destY = dest.y;
        g_destZ = dest.z;
        g_worldAtEmit = world;
        g_targetName = c->getName();

        std::ostringstream line;
        line << "POC-011: EMITINDO setDestination para \"" << g_targetName
             << "\" de (" << start.x << "," << start.y << "," << start.z
             << ") para (" << dest.x << "," << dest.y << "," << dest.z
             << "). NOTA: personagem pode ter sido posicionado pelo "
             << "jogador; escolha o momento do teste com a colonia em rotina";
        diag::milestone(line.str());

        // A escrita em si — a fatia mínima do write-path (HYP-001).
        c->setDestination(dest, false);

        g_phase = PHASE_OBSERVE;
        return;
    }

    diag::log("POC-011: nenhum candidato ocioso/apto nesta rodada; "
              "tentando na proxima");
}

void observe(GameWorld* world) {
    // Detectores de troca de mundo (achado da revisao: save/load no
    // meio da janela invalidaria o veredito silenciosamente).
    if (static_cast<const void*>(world) != g_worldAtEmit) {
        resetExperiment("instancia de GameWorld mudou (load/novo jogo?)");
        return;
    }

    Character* c = resolveTarget();
    if (c == 0) {
        diag::error("POC-011 VEREDITO INCONCLUSIVO: handle do alvo nao "
                    "resolve mais (personagem removido/handle invalido)");
        g_phase = PHASE_DONE;
        return;
    }
    if (c->getName() != g_targetName) {
        resetExperiment("handle resolveu para OUTRO personagem "
                        "(provavel save/load durante a janela)");
        return;
    }

    Ogre::Vector3 pos = c->getPosition();
    Ogre::Vector3 start(g_startX, g_startY, g_startZ);
    Ogre::Vector3 dest(g_destX, g_destY, g_destZ);
    float distFromStart = pos.distance(start);
    float distToDest = pos.distance(dest);
    bool ko = c->isUnconcious();

    std::ostringstream line;
    line << "POC-011: observando \"" << c->getName()
         << "\" pos=(" << pos.x << "," << pos.y << "," << pos.z
         << ") desl-inicio=" << distFromStart
         << "m dist-destino=" << distToDest << "m"
         << (ko ? " INCONSCIENTE" : "");
    CharBody* body = c->getBody();
    if (body != 0) {
        Tasker* action = body->getCurrentAction();
        if (action != 0) {
            line << " acao{key=" << static_cast<int>(action->key());
            const std::string& desc = action->getDescription();
            if (!desc.empty())
                line << " \"" << desc << "\"";
            line << "}";
        } else {
            line << (body->isIdle() ? " ocioso" : " sem-acao");
        }
    }
    diag::log(line.str());

    // Veredito positivo: chegou PERTO DO DESTINO pedido, ou progrediu
    // mais de meio offset no eixo pedido (+x). Deslocamento a partir da
    // origem NAO basta (achado da revisao: mover por ordem do jogador,
    // colisao ou combate viraria falso positivo no Gate A/B).
    bool nearDest = distToDest < 2.0f;
    bool progressedOnAxis = (pos.x - g_startX) > (LS_POC011_OFFSET_METERS * 0.5f);
    if (nearDest || progressedOnAxis) {
        std::ostringstream v;
        v << "POC-011 VEREDITO POSITIVO: write-path funciona -- personagem "
          << (nearDest ? "chegou ao destino pedido" : "progrediu no eixo pedido")
          << " (dist-destino=" << distToDest
          << "m) apos setDestination (insumo do Gate A/B)";
        diag::milestone(v.str());
        g_phase = PHASE_DONE;
        return;
    }

    // Movimento grande que NAO aproxima do destino: causa externa
    // provavel (ordem do jogador, combate, teleporte, load nao
    // detectado) — inconclusivo, nao positivo.
    if (distFromStart > 50.0f) {
        resetExperiment("movimento anomalo para longe do destino "
                        "(causa externa provavel)");
        return;
    }

    --g_observeLeft;
    if (g_observeLeft <= 0) {
        std::ostringstream v;
        v << "POC-011 VEREDITO NEGATIVO/INCONCLUSIVO: nao aproximou do "
             "destino na janela de observacao (dist-destino=" << distToDest
          << "m" << (ko ? ", personagem INCONSCIENTE" : "")
          << ") -- investigar (rota bloqueada? ordem ignorada? unidade "
             "de tempo errada?)";
        diag::milestone(v.str());
        g_phase = PHASE_DONE;
    }
}

} // namespace

void poc011Tick(GameWorld* world) {
    if (world == 0)
        return;

    switch (g_phase) {
    case PHASE_GRACE:
        // Carência: deixa POC-001/002 rodarem algumas rodadas antes de
        // qualquer escrita (leitura primeiro — filosofia da Etapa 0).
        if (g_graceLeft > 0) {
            --g_graceLeft;
            std::ostringstream line;
            line << "POC-011: carencia (" << g_graceLeft
                 << " rodada(s) restante(s) antes de emitir)";
            diag::log(line.str());
            return;
        }
        tryEmit(world);
        break;
    case PHASE_OBSERVE:
        observe(world);
        break;
    case PHASE_DONE:
    default:
        break; // um experimento por PROCESSO; silencioso depois do veredito
    }
}

} // namespace pocs
} // namespace ls
