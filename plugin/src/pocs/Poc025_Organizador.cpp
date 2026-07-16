// Living Settlements -- pocs/Poc025_Organizador.cpp
// ASCII-only. So API verificada. Escreve APENAS via OrderEmitter, atras da cerca.
// GUARDRAILS: nenhum laco sobre contagem nativa sem cap duro (o hang e pior que o crash).
//
// O CEREBRO (v2 -- feedback R2/R3/R4 do dono): a alocacao deixa de ser "1 por predio,
// pega o que da". Agora, atras da cerca de thread (inv.21):
//   R2 multi-slot   -- enche cada estacao ate numOperatorsMax (minas tem 3), nao 1.
//   R3 concentracao -- ranqueia as estacoes por NECESSIDADE (produtiva > faminta >
//                      entupida; maior capacidade livre; veio mais rico) e ENCHE a de
//                      maior retorno ate a capacidade antes de passar a proxima, em vez
//                      de espalhar 1 em cada.
//   R4 skill-aware  -- para cada vaga, escolhe o worker OCIOSO de MAIOR skill no stat
//                      exigido pela estacao (UseableStuff::getStatUsed). Leigos sobram
//                      para os postos menos criticos (ou ficam de fora), nao viram
//                      mineiro sem forca.
// Limitacao honesta: a escolha e gulosa por-estacao (nao uma atribuicao global otima);
// e uma melhora clara sobre o v1, nao o otimo teorico. Ver docs/design/cerebro-declarativo.md sec.7.
#include "pocs/Poc025_Organizador.h"
#include "core/LsConfig.h"
#include "core/PocEnv.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "adapters/OrderEmitter.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/CharStats.h>
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
#include <algorithm> // std::sort
#include <map>
#include <string>
#include <vector>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Caps duros (guardrail do hang).
static const uint32_t ORG_MAX_CHARS       = 512; // personagens do jogador a varrer
static const int      ORG_MAX_CLEAR_PER   = 128; // remocoes por personagem
static const int      ORG_MAX_AVAIL       = 256; // ociosos coletados por rodada
static const int      ORG_MAX_STATIONS    = 512; // estacoes candidatas por rodada
static const int      ORG_MAX_ASSIGN      = 64;  // atribuicoes por rodada

enum OrgPhase { ORG_CLEAR = 0, ORG_RUN };
OrgPhase      g_phase = ORG_CLEAR;
unsigned long g_round = 0;
unsigned long g_lastLog = 0;

// Quantos operadores JA atribuimos a cada predio (uid), nesta sessao. Substitui o
// antigo set booleano "staffado sim/nao" -- que era INCOMPATIVEL com multi-slot
// (R2): impedia a 2a/3a pessoa numa mina. A vaga efetiva de uma estacao =
// numOperatorsMax - max(currentOperators, o que NOS ja atribuimos). O max cobre a
// defasagem entre atribuir e o worker chegar fisicamente ao currentOperators (sem
// isso, super-atribuiria enquanto o worker faz pathing). Reset natural ao recarregar a DLL.
std::map<std::string, int> g_assignedTo;

// Candidata a receber operadores nesta rodada (lida atras da cerca de thread).
struct Cand {
    Building*   b;
    std::string uid;
    int         dt;        // TaskType default da estacao (getDefaultTask)
    int         statUsed;  // stat exigido (-1 = n/a)
    int         freeSlots; // vagas efetivas (>=0), ja descontando nativos + nossos
    int         tier;      // 3=produtiva 2=faminta 1=entupida (impossivel e descartada)
    double      vein;      // riqueza do veio (desempate; -1 = n/a)
    double      dist2;     // distancia^2 ao centro da base (ultimo desempate)
};

// Ordena por NECESSIDADE (maior primeiro): produtiva antes de faminta; maior
// capacidade livre (concentrar nas estacoes que comportam mais); veio mais rico;
// por fim mais perto do centro. Determinismo total (sem empates ambiguos por ptr).
struct CandMoreNeedy {
    bool operator()(const Cand& a, const Cand& c) const {
        if (a.tier != c.tier)             return a.tier > c.tier;
        if (a.freeSlots != c.freeSlots)   return a.freeSlots > c.freeSlots;
        if (a.vein != c.vein)             return a.vein > c.vein;
        if (a.dist2 != c.dist2)           return a.dist2 < c.dist2;
        return a.uid < c.uid;             // desempate estavel final
    }
};

bool eligible(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

// R1: o char que o jogador tem SELECIONADO nao deve receber cargo -- enquanto
// selecionado, o Kenshi trata como controle manual e nao executa o permajob sozinho
// (o char so "anda" ao deselecionar). Casa com WorkerView::isAuthorizableTarget() do
// nucleo. Usa o selectedCharacter singular (confiavel p/ selecao AO VIVO; multi-select
// via caixa nao e coberto -- o probe R1-SEL confirma o mecanismo antes de refinar).
bool isSelectedByPlayer(PlayerInterface* pl, Character* c) {
    if (pl == 0 || c == 0) {
        return false;
    }
    hand sel = pl->selectedCharacter;
    return sel.isValid() && sel.getCharacter() == c;
}

Character* firstEligible(PlayerInterface* pl) {
    lektor<Character*>& chars = pl->playerCharacters;
    uint32_t n = chars.size();
    if (n > ORG_MAX_CHARS) {
        n = ORG_MAX_CHARS;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (eligible(chars[i])) {
            return chars[i];
        }
    }
    return 0;
}

// Skill do char no stat exigido pela estacao. Config do personagem (CharStats), NAO
// mutada em worker thread -> leitura segura sempre (o SnapshotBuilder ja le assim, fora
// do ramo threadSafe). -1 = desconhecido / estacao sem stat exigido.
int skillOf(Character* c, int statUsed) {
    if (c == 0 || statUsed < 0) {
        return -1;
    }
    CharStats* st = c->getStats();
    if (st == 0) {
        return -1;
    }
    float lvl = st->getStat(static_cast<StatsEnumerated>(statUsed), false);
    return static_cast<int>(lvl);
}

// FASE 1: remove TODOS os permajobs de TODOS os chars do jogador (one-shot).
void clearAllJobs(PlayerInterface* pl, core::CoordMode mode,
                  const core::WriteFence& fence, int& charsHit, int& totalCleared) {
    charsHit = 0; totalCleared = 0;
    lektor<Character*>& chars = pl->playerCharacters;
    uint32_t nc = chars.size();
    if (nc > ORG_MAX_CHARS) {
        nc = ORG_MAX_CHARS;
    }
    for (uint32_t i = 0; i < nc; ++i) {
        Character* c = chars[i];
        if (!eligible(c)) {
            continue;
        }
        int removed = 0;
        for (int k = 0; k < ORG_MAX_CLEAR_PER; ++k) {
            if (c->getPermajobCount() <= 0) {
                break;
            }
            // remove sempre o slot 0 (a lista desce); gated no OrderEmitter.
            if (adapters::emitRemovePermajob(mode, fence, c, 0) != adapters::EMIT_OK) {
                break; // cerca fechou / nao-autoridade -> para neste char
            }
            ++removed;
        }
        if (removed > 0) {
            ++charsHit;
            totalCleared += removed;
        }
    }
    // Limpou tudo: zera tambem a contabilidade das nossas atribuicoes (recomeco limpo).
    g_assignedTo.clear();
}

} // namespace

void poc025OrganizadorTick(GameWorld* world) {
    // MODO CONTINUO (LS_ORCHESTRATOR=1, produto): roda TODA rodada, NUNCA
    // limpa cargos existentes -- so cobre vaga com ocioso-livre (0 cargos),
    // por necessidade e skill. O modo experimento antigo (LS_ENABLE_ORGANIZER,
    // compile-time) mantem a fase de limpeza one-shot.
    bool continuous = core::pocEnv().orchestrator;
    if ((!LS_ENABLE_ORGANIZER && !continuous) || world == 0) {
        return;
    }
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return;
    }
    PlayerInterface* pl = world->player;
    if (pl == 0) {
        return;
    }
    Character* anchor = firstEligible(pl);
    if (anchor == 0) {
        return;
    }
    TownBase* town = anchor->getCurrentTownLocation();
    if (town == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    ++g_round;
    if (!core::writeGateOpen(mode, fence)) {
        if (g_round - g_lastLog >= 6) {
            diag::log("ORG: aguardando a cerca de escrita abrir (save/load/filas)");
            g_lastLog = g_round;
        }
        return;
    }

    // ---- FASE 1: LIMPAR TUDO (one-shot; SO no modo experimento antigo) ----
    if (g_phase == ORG_CLEAR) {
        if (continuous) {
            g_phase = ORG_RUN; // produto: aditivo, jamais limpa o que existe
        } else {
            int charsHit = 0, totalCleared = 0;
            clearAllJobs(pl, mode, fence, charsHit, totalCleared);
            std::ostringstream s;
            s << "ORG CLEAR: removidos " << totalCleared << " cargos de " << charsHit
              << " personagens. Agora o cerebro organiza a producao da base.";
            diag::milestone(s.str());
            g_phase = ORG_RUN;
            return;
        }
    }

    // ---- FASE 2: ORGANIZAR (cerebro inteligente; idempotente) ----
    // 1) coleta trabalhadores OCIOSOS (0 permajobs, elegiveis).
    Character* avail[ORG_MAX_AVAIL];
    bool       used[ORG_MAX_AVAIL];
    int navail = 0;
    int skippedSelected = 0;
    {
        lektor<Character*>& chars = pl->playerCharacters;
        uint32_t nc = chars.size();
        if (nc > ORG_MAX_CHARS) {
            nc = ORG_MAX_CHARS;
        }
        for (uint32_t i = 0; i < nc && navail < ORG_MAX_AVAIL; ++i) {
            Character* c = chars[i];
            if (!eligible(c)) {
                continue;
            }
            if (c->getPermajobCount() != 0) {
                continue; // ja tem cargo (nosso desta sessao / pathing)
            }
            if (isSelectedByPlayer(pl, c)) {
                ++skippedSelected; // R1: nao mexer em quem o jogador esta comandando
                continue;
            }
            used[navail] = false;
            avail[navail++] = c;
        }
    }
    if (navail == 0) {
        if (g_round - g_lastLog >= 10) {
            diag::log("ORG: nenhum trabalhador ocioso -- base organizada (no-op).");
            g_lastLog = g_round;
        }
        return;
    }

    // GATE DE THREAD (inv.21): a fase de atribuicao le currentOperators (std::set
    // mutado na WORKER THREAD do Kenshi). Ler durante a mutacao = data race = access
    // violation (foi a causa do crash). A cerca `open` so garante save/load; a leitura
    // de membros de worker-thread exige `threadsClear`. Adiar quando as filas nao estao
    // limpas (a passada e idempotente).
    if (!fence.threadsClear) {
        if (g_round - g_lastLog >= 10) {
            diag::log("ORG: filas nao-limpas -- adiando a organizacao (inv.21, "
                      "seguranca de thread p/ ler currentOperators).");
            g_lastLog = g_round;
        }
        return;
    }

    // 2) coleta as estacoes de PRODUCAO da base como candidatas, com sua NECESSIDADE.
    Ogre::Vector3 center = town->getPosition();
    float radius = LS_M0_RADIUS;
    {
        float tr = town->getRadius();
        if (tr > radius) {
            radius = tr;
        }
    }
    std::vector<Cand> cands;
    lektor<RootObject*> results;
    world->getObjectsWithinSphere(results, center, radius, BUILDING,
                                  LS_M0_MAX_RESULTS, 0);
    for (uint32_t i = 0; i < results.size()
                      && static_cast<int>(cands.size()) < ORG_MAX_STATIONS; ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o);
        UseableStuff* us = b->getUseableStuff();
        ProductionBuilding* pb = b->getProductionBuilding();
        if (us == 0 || pb == 0) {
            continue; // so postos de producao operaveis
        }
        if (!us->needsOperating || us->isBroken()) {
            continue;
        }
        if (b->getTown() != town) {
            continue; // so a base (a town do anchor)
        }
        TaskType dt = us->getDefaultTask();
        if (dt == NULL_TASK) {
            continue;
        }
        InstanceID* iid = b->getInstanceID();
        std::string uid = (iid != 0) ? iid->uid : std::string();

        // Vaga efetiva (R2): capacidade - max(ocupantes atuais, o que nos ja atribuimos).
        int cur = static_cast<int>(us->currentOperators.size());
        int ours = 0;
        if (!uid.empty()) {
            std::map<std::string, int>::iterator it = g_assignedTo.find(uid);
            if (it != g_assignedTo.end()) {
                ours = it->second;
            }
        }
        int occupied = (cur > ours) ? cur : ours;
        int freeSlots = us->numOperatorsMax - occupied;
        if (freeSlots <= 0) {
            continue; // ja cheia (nativos + nossos)
        }

        // Tier de necessidade (R3): produtiva > faminta > entupida; impossivel descartada.
        int tier;
        switch (pb->productionState) {
            case ProductionBuilding::PRODUCTION_NORMAL:     tier = 3; break;
            case ProductionBuilding::PRODUCTION_STARVED:    tier = 2; break;
            case ProductionBuilding::PRODUCTION_FULL:       tier = 1; break;
            case ProductionBuilding::PRODUCTION_IMPOSSIBLE: tier = 0; break;
            default:                                        tier = 3; break; // operavel
        }
        if (tier == 0) {
            continue; // cadeia quebrada: um operador so ficaria parado
        }

        Cand cd;
        cd.b = b;
        cd.uid = uid;
        cd.dt = static_cast<int>(dt);
        cd.statUsed = static_cast<int>(us->getStatUsed());
        cd.freeSlots = freeSlots;
        cd.tier = tier;
        cd.vein = pb->getMiningResourceLevel();
        Ogre::Vector3 bp = b->getPosition();
        double ddx = bp.x - center.x, ddy = bp.y - center.y, ddz = bp.z - center.z;
        cd.dist2 = ddx * ddx + ddy * ddy + ddz * ddz;
        cands.push_back(cd);
    }
    if (results.stuff != 0) { // so o array de RootObject*; os Building* seguem validos.
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }

    // 3) ranqueia por necessidade e ENCHE cada estacao ate a capacidade (concentracao),
    //    escolhendo o worker de MAIOR skill no stat exigido (R4), antes da proxima.
    std::sort(cands.begin(), cands.end(), CandMoreNeedy());

    int assigned = 0;
    for (size_t ci = 0; ci < cands.size() && assigned < ORG_MAX_ASSIGN; ++ci) {
        Cand& cd = cands[ci];
        while (cd.freeSlots > 0 && assigned < ORG_MAX_ASSIGN) {
            // seleciona o ocioso disponivel de maior skill no stat da estacao (R4).
            int best = -1, bestSkill = -0x7fffffff;
            for (int w = 0; w < navail; ++w) {
                if (used[w]) {
                    continue;
                }
                int sk = skillOf(avail[w], cd.statUsed);
                if (sk > bestSkill) {
                    bestSkill = sk;
                    best = w;
                }
            }
            if (best < 0) {
                break; // acabaram os ociosos -> encerra o preenchimento
            }
            Character* worker = avail[best];
            hand wh(worker); // Character* -> RootObjectBase*
            UseableStuff* us = cd.b->getUseableStuff();
            if (us == 0 || !us->isFreeSlot(wh)) {
                // guarda final por-worker: sem vaga real p/ este worker nesta estacao.
                // Nao consome o worker; encerra esta estacao (a contagem discorda do nativo).
                break;
            }
            Ogre::Vector3 bp = cd.b->getPosition();
            adapters::EmitResult r =
                adapters::emitAddPermajob(mode, fence, worker, cd.b,
                                          static_cast<TaskType>(cd.dt), bp);
            if (r == adapters::EMIT_OK) {
                used[best] = true;
                --cd.freeSlots;
                ++assigned;
                if (!cd.uid.empty()) {
                    g_assignedTo[cd.uid] += 1;
                }
                std::ostringstream s;
                s << "  ORG staff: \"" << worker->getName() << "\" (skill=";
                if (cd.statUsed < 0) {
                    s << "n/a";
                } else {
                    s << bestSkill;
                }
                s << ") -> predio " << (cd.uid.empty() ? std::string("?") : cd.uid)
                  << " tier=" << cd.tier << " vaga_restante=" << cd.freeSlots
                  << " task=" << cd.dt;
                diag::log(s.str());
            } else {
                // worker problematico (nao-autoridade): consome-o (nao trava a estacao).
                used[best] = true;
            }
        }
    }

    if (assigned > 0) {
        std::ostringstream s;
        s << "ORG RUN #" << g_round << ": " << assigned << " operadores alocados nesta "
          << "rodada (" << navail << " ociosos; " << cands.size()
          << " estacoes com vaga; ranqueadas por necessidade, cheias ate a capacidade, "
          << "skill-aware";
        if (skippedSelected > 0) {
            s << "; pulou " << skippedSelected << " selecionado(s) [R1]";
        }
        s << ").";
        diag::milestone(s.str());
    } else if (g_round - g_lastLog >= 10) {
        std::ostringstream s;
        s << "ORG RUN #" << g_round << ": 0 alocacoes (" << navail
          << " ociosos, mas nenhuma estacao de producao com vaga na base).";
        diag::log(s.str());
        g_lastLog = g_round;
    }
}

} // namespace pocs
} // namespace ls
