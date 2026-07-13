// Living Settlements -- pocs/Poc022_H11.cpp
// P5-3: PRIMEIRA ESCRITA controlada (prova H11). ASCII-only. So simbolos
// verificados nos headers reais (sec.0.2). Escreve APENAS via OrderEmitter
// (choke unico) e APENAS atras de writeGateOpen (modo + save-fence + filas).
#include "pocs/Poc022_H11.h"
#include "core/LsConfig.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "adapters/OrderEmitter.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/Tasker.h>
#include <kenshi/Town.h>
#include <kenshi/InstanceID.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/Building/ProductionBuilding.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <cstdlib>   // free()
#include <string>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Fase do experimento (main thread; single-shot). Reset natural ao recarregar
// a DLL (novo processo). Apos H11_DONE, nunca mais age nesta sessao.
enum Phase { H11_ARMED = 0, H11_OBSERVING, H11_DONE };
Phase         g_phase = H11_ARMED;
std::string   g_worker;      // nome do worker escolhido
std::string   g_mineId;      // uid da mina
int           g_taskNative = -1;
int           g_baseline = -1;   // getPermajobCount antes do addJob
unsigned long g_round = 0;
unsigned long g_emitRound = 0;
bool          g_verdictLogged = false;
unsigned long g_lastWaitLog = 0; // throttle do "aguardando cerca"

// Orcamento de linhas DIAG p/ a sessao inteira (evita spam).
int g_diagBudget = 60;

// Tamanho de container sem nomear o tipo (selectedCharacters e um
// ogre_unordered_set<hand>). Distingue "selecao vazia" de "task invalido".
template <class C>
size_t containerSize(const C& c) {
    return c.size();
}

Character* firstPlayerChar(GameWorld* world) {
    if (world == 0 || world->player == 0) {
        return 0;
    }
    lektor<Character*>& chars = world->player->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        if (chars[i] != 0) {
            return chars[i];
        }
    }
    return 0;
}

Character* findCharByName(GameWorld* world, const std::string& name) {
    if (world == 0 || world->player == 0) {
        return 0;
    }
    lektor<Character*>& chars = world->player->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        Character* c = chars[i];
        if (c != 0 && c->getName() == name) {
            return c;
        }
    }
    return 0;
}

// Alvo minimamente valido p/ o experimento: existe, vivo/consciente, aceita
// ordens, nao e animal. (NAO exige "maos vazias": getTotalCarryWeight inclui
// equipamento -> quase ninguem tem <eps; e um one-shot, sem risco de oscilacao.)
bool eligibleTarget(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

// Escolha do worker, por prioridade (g_pickPath registra qual via, p/ o log):
//   1) LS_H11_WORKER (nome exato) -- alvo fixo.
//   2) o personagem SELECIONADO pelo jogador -- o "sujeito de teste" que ELE
//      escolhe no jogo (some com o problema de "nenhum worker livre"; a
//      selecao E o opt-in deste experimento).
//   3) fallback: 1o ocioso-livre (0 cargos) -- espelho do pool do Marco 0.
const char* g_pickPath = "?";
Character* pickWorker(GameWorld* world) {
    PlayerInterface* pl = world->player;
    if (pl == 0) {
        return 0;
    }
    if (LS_H11_WORKER[0] != '\0') {
        Character* c = findCharByName(world, LS_H11_WORKER);
        if (eligibleTarget(c)) { g_pickPath = "nome"; return c; }
        return 0;
    }
    hand sel = pl->selectedCharacter;
    if (sel.isValid()) {
        Character* c = sel.getCharacter();
        if (eligibleTarget(c)) { g_pickPath = "selecionado"; return c; }
    }
    lektor<Character*>& chars = pl->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        Character* c = chars[i];
        if (!eligibleTarget(c)) {
            continue;
        }
        if (c->getPermajobCount() != 0) {
            continue; // livre (0 cargos)
        }
        CharBody* b = c->getBody();
        if (b == 0 || !b->isIdle()) {
            continue; // ocioso
        }
        g_pickPath = "auto-ocioso";
        return c;
    }
    return 0;
}

// Mina da BASE DO WORKER, operavel e com vaga: BF_MINE(_NATURAL), producao,
// precisa operar, nao quebrada, vaga p/ a mao dele, task default valida, e --
// FILTRO DE DONO -- na MESMA town do worker (getTown()==wTown). Minas de OUTRA
// town o jogo ate "atribui", mas PODA em seguida (JOBHOOK: minas zone26.33 caiam
// p/ 0; a base zone25.33 colou). getPlayerTaskProbability NAO serve de gate (deu
// 0 ate p/ minas atribuiveis manualmente). Escolhe a mais proxima. Task = o
// getDefaultTask da estacao (87 manual / 152 automatica -- casa com o JOBHOOK).
Building* pickMine(GameWorld* world, TownBase* town, Character* w,
                   int& seen, int& freeSlot, TaskType& chosenTask) {
    seen = 0; freeSlot = 0; chosenTask = NULL_TASK;
    Ogre::Vector3 center = town->getPosition();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    lektor<RootObject*> results;
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);
    hand wHand(w); // Character* -> RootObjectBase* (ctor hand.h:23)
    TownBase* wTown = w->getCurrentTownLocation(); // town DO worker (a base)
    Ogre::Vector3 wp = w->getPosition();
    Building* best = 0;
    double bestD2 = 0.0;
    TaskType bestTask = NULL_TASK;
    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o);
        BuildingFunction fn = b->getSpecialFunction();
        if (!(fn == BF_MINE || fn == BF_MINE_NATURAL)) {
            continue;
        }
        UseableStuff* us = b->getUseableStuff();
        if (us == 0 || b->getProductionBuilding() == 0) {
            continue;
        }
        ++seen;
        if (!us->needsOperating || us->isBroken()) {
            continue;
        }
        if (!us->isFreeSlot(wHand)) {
            continue; // sem vaga p/ a mao dele
        }
        ++freeSlot;
        // FILTRO DE DONO: mesma town do worker (a base). Sem ele, minas alheias
        // sao "atribuidas" e podadas -- nao provariam durabilidade.
        if (wTown != 0 && b->getTown() != wTown) {
            continue;
        }
        TaskType dt = us->getDefaultTask();
        if (dt == NULL_TASK) {
            continue;
        }
        if (g_diagBudget > 0) {
            InstanceID* iid = b->getInstanceID();
            std::ostringstream s;
            s << "  DIAG mina-base=" << (iid != 0 ? iid->uid : std::string("?"))
              << " fn=" << static_cast<int>(fn) << " task=" << static_cast<int>(dt);
            diag::log(s.str());
            --g_diagBudget;
        }
        Ogre::Vector3 bp = b->getPosition();
        double dx = bp.x - wp.x, dy = bp.y - wp.y, dz = bp.z - wp.z;
        double d2 = dx * dx + dy * dy + dz * dz;
        if (best == 0 || d2 < bestD2) {
            best = b;
            bestD2 = d2;
            bestTask = dt;
        }
    }
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
    chosenTask = bestTask;
    return best;
}

// Template p/ nao nomear o allocator do std::set<hand> (mesmo padrao do
// SnapshotBuilder). So lido com filas limpas.
template <class SetT>
bool setHasChar(const SetT& ops, Character* w) {
    for (typename SetT::const_iterator it = ops.begin(); it != ops.end(); ++it) {
        hand h = *it;
        if (h.isValid() && h.getCharacter() == w) {
            return true;
        }
    }
    return false;
}

// Despeja a lista COMPLETA de permajobs do worker (mesma leitura do AIPROBE).
// Usado no MESMO tick da escrita p/ ver exatamente o que o addJob produziu
// (se apareceu, com qual TaskType, e em qual subject) antes de qualquer poda
// do GOAP. Cap defensivo de 64 (guardrail do hang). So lido com filas limpas.
void dumpPermajobs(Character* w, const char* tag) {
    int n = w->getPermajobCount();
    {
        std::ostringstream h;
        h << "H11 DUMP[" << tag << "] \"" << w->getName()
          << "\": permajobCount=" << n;
        diag::log(h.str());
    }
    if (n > 64) {
        n = 64;
    }
    for (int s = 0; s < n; ++s) {
        TaskType k = w->getPermajob(s);
        std::string nm = w->getPermajobName(s);
        const Tasker* tk = w->getPermajobData(s);
        std::string uid;
        if (tk != 0) {
            hand subj = tk->subject;
            if (subj.isValid()) {
                Building* b = subj.getBuilding();
                if (b != 0) {
                    InstanceID* iid = b->getInstanceID();
                    if (iid != 0) {
                        uid = iid->uid;
                    }
                }
            }
        }
        std::ostringstream s2;
        s2 << "    [" << s << "] key=" << static_cast<int>(k) << " \"" << nm
           << "\" subj=" << (uid.empty() ? std::string("(-)") : uid);
        diag::log(s2.str());
    }
}

// Acha o slot do NOSSO permajob (subject -> mina == g_mineId) e a mina. [H3/F8]
int findOurSlot(Character* w, std::string& nameOut, Building*& mineOut) {
    nameOut.clear();
    mineOut = 0;
    int n = w->getPermajobCount();
    for (int s = 0; s < n; ++s) {
        const Tasker* tk = w->getPermajobData(s);
        if (tk == 0) {
            continue;
        }
        hand subj = tk->subject;
        if (!subj.isValid()) {
            continue;
        }
        Building* b = subj.getBuilding();
        if (b == 0) {
            continue;
        }
        InstanceID* iid = b->getInstanceID();
        if (iid != 0 && iid->uid == g_mineId) {
            nameOut = w->getPermajobName(s);
            mineOut = b;
            return s;
        }
    }
    return -1;
}

} // namespace

void poc022H11Tick(GameWorld* world) {
    if (!LS_ENABLE_H11 || g_phase == H11_DONE || world == 0) {
        return;
    }
    // H11 tem seu PROPRIO switch de escrita (o coordenador geral segue sombra):
    // avalia o modo com writesEnabled=true e so age atras da cerca completa.
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return; // load/reset/mundo ausente -> nem tentar
    }
    Character* anchor = firstPlayerChar(world);
    if (anchor == 0) {
        return;
    }
    TownBase* town = anchor->getCurrentTownLocation();
    if (town == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    ++g_round;

    // ---- ARMADO: escolhe alvo e emite UMA vez (metodo corrente da escada) ----
    if (g_phase == H11_ARMED) {
        bool throttle = (g_round - g_lastWaitLog) >= 6;
        if (!core::writeGateOpen(mode, fence)) {
            if (throttle) {
                diag::log("H11 ARMADO: aguardando a cerca de escrita abrir "
                          "(save/load/filas)");
                g_lastWaitLog = g_round;
            }
            return;
        }
        PlayerInterface* pl = world->player;
        if (pl == 0) {
            return;
        }
        Character* w = pickWorker(world);
        if (w == 0) {
            if (throttle) {
                diag::log("H11 ARMADO: sem alvo -- SELECIONE um personagem no jogo "
                          "(as funcoes da UI agem na SELECAO)");
                g_lastWaitLog = g_round;
            }
            return;
        }
        int seen = 0, freeSlot = 0;
        TaskType chosenTask = NULL_TASK;
        Building* m = pickMine(world, town, w, seen, freeSlot, chosenTask);
        if (m == 0) {
            if (throttle) {
                std::ostringstream s;
                s << "H11 ARMADO: sem mina na TOWN do worker \"" << w->getName()
                  << "\" (minas=" << seen << " vaga=" << freeSlot
                  << " selCount=" << containerSize(pl->selectedCharacters)
                  << ") -- selecione um bot NA BASE, perto das suas minas.";
                diag::log(s.str());
                g_lastWaitLog = g_round;
            }
            return;
        }
        TaskType dt = chosenTask;
        InstanceID* mid = m->getInstanceID();
        g_worker = w->getName();
        g_mineId = (mid != 0) ? mid->uid : std::string();
        g_taskNative = static_cast<int>(dt);
        g_baseline = w->getPermajobCount();
        Ogre::Vector3 mp = m->getPosition();

        // ESCRITA PER-CHAR DIRETA (mapa-jobs.md §3, opcao C -- a BASE do produto):
        // target->addJob(87, predio, shift=TRUE, addDontClear=TRUE, pos). shift=TRUE
        // satisfaz a Porta 1 do router (0x507C40); 87 e permajob-capaz (Porta 2) ->
        // permajob DURAVEL@0x88. SEM tocar na selecao (mira por NOME em LS_H11_WORKER)
        // -> resolve tambem o "caiu no char errado" (selecao transitoria no load).
        adapters::EmitResult r =
            adapters::emitAddPermajob(mode, fence, w, m, dt, mp);

        int after = w->getPermajobCount();
        {
            std::ostringstream s;
            s << "H11 EMITIU (per-char addJob shift=TRUE): worker=\"" << g_worker
              << "\" (via " << g_pickPath << ") mina=" << g_mineId
              << " task=" << g_taskNative << " -> " << adapters::emitResultName(r)
              << " cargos:" << g_baseline << "->" << after;
            diag::milestone(s.str());
        }
        dumpPermajobs(w, "pos-emit");

        if (r != adapters::EMIT_OK) {
            return; // bloqueado (cerca/nulo) -> tenta de novo
        }
        if (after > g_baseline) {
            // Criou algo AGORA -> confirmar DURABILIDADE nas proximas rodadas.
            g_phase = H11_OBSERVING;
            g_emitRound = g_round;
            g_verdictLogged = false;
        } else {
            diag::milestone("H11: a chamada da UI NAO criou cargo (count inalterado) "
                            "-- investigar (mina/selecao).");
            g_phase = H11_DONE;
        }
        return;
    }

    // ---- OBSERVANDO: o cargo colou? e reverter (maos vazias) ----
    if (g_phase == H11_OBSERVING) {
        Character* w = findCharByName(world, g_worker);
        if (w == 0) {
            diag::milestone("H11 OBS: worker sumiu do roster -- encerrando");
            g_phase = H11_DONE;
            return;
        }
        int now = w->getPermajobCount();
        std::string ourName;
        Building* mine = 0;
        int ourSlot = findOurSlot(w, ourName, mine);
        bool inOps = false;
        if (fence.threadsClear && mine != 0) {
            UseableStuff* us = mine->getUseableStuff();
            if (us != 0) {
                inOps = setHasChar(us->currentOperators, w);
            }
        }
        unsigned long elapsed = g_round - g_emitRound;
        {
            std::ostringstream s;
            s << "H11 OBS #" << elapsed << ": cargos=" << now
              << " (baseline " << g_baseline << ")";
            if (ourSlot >= 0) {
                s << " | NOSSO cargo slot=" << ourSlot << " \"" << ourName << "\"";
            } else {
                s << " | nosso cargo NAO encontrado";
            }
            if (fence.threadsClear) {
                s << (inOps ? " | worker EM currentOperators"
                            : " | worker fora de currentOperators");
            } else {
                s << " | (ops nao-obs)";
            }
            diag::log(s.str());
        }

        if (elapsed < static_cast<unsigned long>(LS_H11_OBSERVE_ROUNDS)) {
            return; // ainda observando
        }

        if (g_verdictLogged) {
            return; // ja decidido
        }
        dumpPermajobs(w, "veredito"); // estado final completo, p/ comparar
        bool durable = (ourSlot >= 0) && (now > g_baseline);
        if (durable) {
            diag::milestone("H11 VEREDITO: CONFIRMADO -- a escrita COLOU: permajob "
                            "DURAVEL (persiste e aponta p/ a mina). Cargo deixado "
                            "ATIVO p/ voce ver o bot ir minerar.");
        } else {
            std::ostringstream d;
            d << "H11 VEREDITO: cargo apareceu mas NAO persistiu (podado apos "
              << elapsed << " rodadas) -- mina provavelmente invalida p/ este worker.";
            diag::milestone(d.str());
        }
        g_verdictLogged = true;
        g_phase = H11_DONE;
        return;
    }
}

} // namespace pocs
} // namespace ls
