// Living Settlements -- pocs/Poc023_AITaskProbe.cpp
// READ-ONLY. ASCII-only. So simbolos verificados nos headers (sec.0.2).
// Despeja os PERMAJOBS do personagem selecionado (via a API do proprio Character,
// mesma que Poc020/022 ja compilam) -> quando o jogador atribui um job de mina
// pelo painel NATIVO do Kenshi, a gente ve o cargo aparecer e QUAL TaskType e.
// (Evita AI/AI.h + AITaskSystem.h: eles redefinem taskPriority/CharacterMessage.)
#include "pocs/Poc023_AITaskProbe.h"
#include "core/LsConfig.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/Tasker.h>
#include <kenshi/Building/Building.h>
#include <kenshi/InstanceID.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>

#include <cstdint>
#include <string>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

unsigned long g_round = 0;
unsigned long g_lastNoSel = 0;

// Banda de prioridade da acao atual -> nome (Tasker.h:11). Permajobs (trabalho de
// fundo) executam em banda baixa; controle manual/ordem direta sobe p/ TP_OBEDIENCE.
const char* priorityBand(int p) {
    switch (p) {
        case TP_JUST_ACTION: return "JUST_ACTION";
        case TP_FLUFF:       return "FLUFF";
        case TP_NON_URGENT:  return "NON_URGENT";
        case TP_URGENT:      return "URGENT";
        case TP_OBEDIENCE:   return "OBEDIENCE";
        default:             return "?";
    }
}

// PROBE R1 do char ATUALMENTE SELECIONADO (qualquer retrato que o jogador clique),
// independente de LS_H11_WORKER. Read-only. Responde: um char selecionado com cargo
// para de operar? A acao atual muda de banda (URGENT->OBEDIENCE) ou vira idle?
void logSelectedR1(PlayerInterface* pl) {
    hand sel = pl->selectedCharacter;      // singular (confiavel p/ selecao AO VIVO)
    if (!sel.isValid()) {
        return;
    }
    Character* c = sel.getCharacter();
    if (c == 0) {
        return;
    }
    int pj = c->getPermajobCount();
    CharBody* body = c->getBody();
    bool idle = false;
    int prio = -1;
    if (body != 0) {
        idle = body->isIdle();
        Tasker* act = body->getCurrentAction();
        if (act != 0) {
            prio = static_cast<int>(act->priority);
        }
    }
    std::ostringstream s;
    s << "AIPROBE R1-SEL \"" << c->getName() << "\": SELECIONADO permajobs=" << pj
      << " idle=" << (idle ? "SIM" : "nao") << " acaoAtual=";
    if (prio < 0) {
        s << "(nenhuma)";
    } else {
        s << priorityBand(prio) << "(" << prio << ")";
    }
    diag::log(s.str());
}

std::string subjectUid(const Tasker* t) {
    hand subj = t->subject;              // Tasker.h:146 (membro publico @0x10)
    if (!subj.isValid()) {
        return std::string();
    }
    Building* b = subj.getBuilding();    // hand.h:52
    if (b == 0) {
        return std::string();
    }
    InstanceID* iid = b->getInstanceID();
    return (iid != 0) ? iid->uid : std::string();
}

} // namespace

void poc023AiProbeTick(GameWorld* world) {
    if (!LS_ENABLE_AIPROBE || world == 0) {
        return;
    }
    // Read-only. Fecha em load/reset; a lista de permajobs e tocada pela IA
    // (worker thread) -> so ler com as filas limpas (inv.21).
    if (core::evaluateLifecycle(world, false) == core::MODE_SKIP) {
        return;
    }
    if (!core::threadReadsSafe(world)) {
        return;
    }
    PlayerInterface* pl = world->player;
    if (pl == 0) {
        return;
    }
    ++g_round;

    // R1: sempre que houver um char SELECIONADO (qualquer retrato clicado), dumpa a
    // banda da acao dele -- decide o mecanismo do R1 sem depender de LS_H11_WORKER.
    logSelectedR1(pl);

    // Alvo: LS_H11_WORKER (nome) ou o personagem SELECIONADO pelo jogador.
    Character* w = 0;
    if (LS_H11_WORKER[0] != '\0') {
        lektor<Character*>& chars = pl->playerCharacters;
        for (uint32_t i = 0; i < chars.size(); ++i) {
            Character* c = chars[i];
            if (c != 0 && c->getName() == std::string(LS_H11_WORKER)) {
                w = c;
                break;
            }
        }
    } else {
        hand sel = pl->selectedCharacter;
        if (sel.isValid()) {
            w = sel.getCharacter();
        }
    }
    if (w == 0) {
        if (g_round - g_lastNoSel >= 6) {
            diag::log("AIPROBE: selecione um personagem p/ inspecionar os cargos "
                      "(permajobs) da IA");
            g_lastNoSel = g_round;
        }
        return;
    }

    int n = w->getPermajobCount();       // Character.h:424
    {
        std::ostringstream s;
        s << "AIPROBE \"" << w->getName() << "\": permajobCount=" << n;
        diag::log(s.str());
    }

    // --- PROBE R1: a SELECAO trava a execucao do cargo? ------------------------
    // Hipotese: enquanto selecionado, a acao atual do char fica numa banda que
    // suprime o permajob (trabalho de fundo) -> so executa ao deselecionar. Este
    // dump (idle + banda da acao atual + selecionado?) permite VER, com o mesmo
    // char selecionado vs. nao, se a banda/idle muda. So leitura (CharBody).
    {
        bool selected = false;
        hand sel = pl->selectedCharacter;                  // singular (live-select)
        if (sel.isValid() && sel.getCharacter() == w) {
            selected = true;
        }
        CharBody* body = w->getBody();
        bool idle = false;
        int prio = -1;
        if (body != 0) {
            idle = body->isIdle();
            Tasker* act = body->getCurrentAction();        // CharBody.h:67
            if (act != 0) {
                prio = static_cast<int>(act->priority);    // Tasker.h:144
            }
        }
        std::ostringstream s;
        s << "AIPROBE R1 \"" << w->getName() << "\": selecionado=" << (selected ? "SIM" : "nao")
          << " idle=" << (idle ? "SIM" : "nao") << " acaoAtual=";
        if (prio < 0) {
            s << "(nenhuma)";
        } else {
            s << priorityBand(prio) << "(" << prio << ")";
        }
        diag::log(s.str());
    }
    if (n > 64) {
        n = 64; // cap defensivo (guardrail do hang)
    }
    for (int slot = 0; slot < n; ++slot) {
        TaskType k = w->getPermajob(slot);           // Character.h:422 (TaskType)
        std::string nm = w->getPermajobName(slot);   // Character.h:421
        const Tasker* t = w->getPermajobData(slot);  // Character.h:423
        std::string uid;
        bool tdPerma = false;
        if (t != 0) {
            uid = subjectUid(t);
            const TaskData* td = t->getTaskData();   // Tasker.h:170
            if (td != 0) {
                tdPerma = td->isPermaJob();          // TaskData -- Tasker.h:81
            }
        }
        std::ostringstream s;
        s << "    permajob[" << slot << "] key=" << static_cast<int>(k)
          << " \"" << nm << "\" subject="
          << (uid.empty() ? std::string("(nao-predio)") : uid)
          << (tdPerma ? " [TaskData.PERMA]" : "");
        diag::log(s.str());
    }
}

} // namespace pocs
} // namespace ls
