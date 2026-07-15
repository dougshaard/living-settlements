// Living Settlements -- pocs/Poc027_Torre.cpp
// POC-TUR-1. ASCII-only. So simbolos verificados no header sweep (sec.0.2):
//   Building::getBuildingClass() / BCTYPE_TURRET       Building.h:249/40
//   UseableStuff::numOperatorsMax/currentOperators/isFreeSlot/isOutOfPower/
//     getDefaultTask                                    UseableStuff.h
//   Character::addJob(TaskType, RootObject*, ...)       Character.h:416
//   Tasker::subject@0x10, taskData@0x70 -> key@0x44     Tasker.h
// Escrita APENAS via OrderEmitter atras de writeGateOpen. Caps duros em todo laco.
//
// CRITERIOS DE CONFIRM (escritos ANTES da sessao):
//   CONFIRM-TUR-1 (criacao+swap): getPermajobCount sobe +1 E aparece slot com
//     key==MAN_A_TURRET_PLAYER_JOB (234) e subject==a torre alvo. Se aparecer
//     key==USE_TURRET (146) em vez de 234 -> ANOMALIA (refutaria o swap 1.2).
//   CONFIRM-TUR-2 (paz, I-24): o cargo PERSISTE na lista; o char pode estar
//     fazendo outra coisa -- NAO e falha. Confirmacao pela LISTA, nunca pela
//     acao corrente.
//   CONFIRM-TUR-3 (alerta): sob ataque/alerta o char sobe na torre e atira
//     (worker aparece em currentOperators da torre; acao corrente key==234).
//   CONFIRM-TUR-4 (lacunas 10/11): numOperatorsMax real logado por torre;
//     isOutOfPower observado (torre sem fiacao bloqueia ou nao?).
//   CONFIRM-TUR-5 (durabilidade+reversao, sessao 2 com LS_POC_REVERT=1): cargo
//     presente apos save/load; removePermajob -> slot 234 some; char liberado.
#include "pocs/Poc027_Torre.h"
#include "core/PocEnv.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "core/LsConfig.h"
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

// Caps duros (guardrail do hang).
static const uint32_t TUR_MAX_CHARS   = 512;
static const int      TUR_MAX_SLOTS   = 64;
static const int      TUR_MAX_REMOVE  = 8;
static const int      TUR_MAX_TURRETS = 64;  // torres detalhadas por rodada

enum TurPhase { TUR_ARMED = 0, TUR_OBSERVING, TUR_DONE };
TurPhase      g_phase = TUR_ARMED;
unsigned long g_round = 0;
unsigned long g_lastWaitLog = 0;
int           g_baseline = -1;
std::string   g_turretUid;    // uid da torre alvo (escolhida na emissao)
bool          g_revertDone = false;
int           g_censusBudget = 3; // rodadas com censo DETALHADO de torres

Character* findCharByName(GameWorld* world, const std::string& name) {
    if (world == 0 || world->player == 0 || name.empty()) {
        return 0;
    }
    lektor<Character*>& chars = world->player->playerCharacters;
    uint32_t n = chars.size();
    if (n > TUR_MAX_CHARS) {
        n = TUR_MAX_CHARS;
    }
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c != 0 && c->getName() == name) {
            return c;
        }
    }
    return 0;
}

bool eligibleTarget(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

std::string uidOf(Building* b) {
    InstanceID* iid = b->getInstanceID();
    return (iid != 0) ? iid->uid : std::string();
}

// Slot do NOSSO cargo de torre: key==MAN_A_TURRET_PLAYER_JOB e subject==uid da
// torre alvo ("" casa qualquer torre). fixedTarget=1 -> pode haver N cargos 234
// (um por torre); o filtro por uid mantem a remocao cirurgica.
int findTurretSlot(Character* w, const std::string& turretUid,
                   bool* saw146 /*anomalia: 146 na lista*/) {
    if (saw146 != 0) {
        *saw146 = false;
    }
    int n = w->getPermajobCount();
    if (n > TUR_MAX_SLOTS) {
        n = TUR_MAX_SLOTS;
    }
    for (int s = 0; s < n; ++s) {
        TaskType k = w->getPermajob(s);
        if (k == USE_TURRET && saw146 != 0) {
            *saw146 = true; // 146 fixado cru = swap NAO aconteceu (anomalia)
        }
        if (k != MAN_A_TURRET_PLAYER_JOB) {
            continue;
        }
        if (turretUid.empty()) {
            return s;
        }
        const Tasker* tk = w->getPermajobData(s);
        if (tk == 0) {
            continue;
        }
        hand subj = tk->subject;
        if (!subj.isValid()) {
            continue;
        }
        Building* b = subj.getBuilding();
        if (b != 0 && uidOf(b) == turretUid) {
            return s;
        }
    }
    return -1;
}

void dumpPermajobs(Character* w, const char* tag) {
    int n = w->getPermajobCount();
    {
        std::ostringstream h;
        h << "TUR DUMP[" << tag << "] \"" << w->getName()
          << "\": permajobCount=" << n;
        diag::log(h.str());
    }
    if (n > TUR_MAX_SLOTS) {
        n = TUR_MAX_SLOTS;
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
                    uid = uidOf(b);
                }
            }
        }
        std::ostringstream s2;
        s2 << "    [" << s << "] key=" << static_cast<int>(k) << " \"" << nm
           << "\" subj=" << (uid.empty() ? std::string("(-)") : uid);
        diag::log(s2.str());
    }
}

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

int currentActionKey(Character* w) {
    CharBody* body = w->getBody();
    if (body == 0) {
        return -1;
    }
    Tasker* action = body->getCurrentAction();
    if (action == 0) {
        return -1;
    }
    const TaskData* td = action->taskData;
    if (td == 0) {
        return -1;
    }
    return static_cast<int>(td->key);
}

// Enumera as TORRES da base (getBuildingClass==BCTYPE_TURRET, ctor-stamped;
// mapa sec.2 TUR-10 -- NAO usar specialFunction p/ isto). detailed=true loga
// cada torre (lacunas 10/11: numOperatorsMax real + energia). Retorna a torre
// alvo: uid exato (LS_POC_TURRET) ou a mais proxima do worker. Ponteiros
// tick-scoped (nunca cacheados entre rodadas).
Building* findTurrets(GameWorld* world, TownBase* town, Character* w,
                      const std::string& wantUid, bool threadsClear,
                      bool detailed, int& turretsSeen) {
    turretsSeen = 0;
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
    Ogre::Vector3 wp = w->getPosition();
    Building* best = 0;
    double bestD2 = 0.0;
    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o); // filtro BUILDING garante o tipo
        if (b->getBuildingClass() != BCTYPE_TURRET) {
            continue;
        }
        if (b->getTown() != town) {
            continue; // so a base
        }
        ++turretsSeen;
        UseableStuff* us = b->getUseableStuff();
        if (detailed && turretsSeen <= TUR_MAX_TURRETS) {
            std::ostringstream s;
            s << "    torre " << uidOf(b);
            if (us != 0) {
                s << " opsMax=" << us->numOperatorsMax
                  << " precisaOperar=" << (us->needsOperating ? 1 : 0)
                  << " quebrada=" << (us->isBroken() ? 1 : 0)
                  << " deficitEnergia=" << us->isOutOfPower()
                  << " taskDefault=" << static_cast<int>(us->getDefaultTask());
                if (threadsClear) {
                    s << " opsAgora=" << us->currentOperators.size();
                } else {
                    s << " opsAgora=(nao-obs)";
                }
            } else {
                s << " (sem UseableStuff!)";
            }
            diag::log(s.str());
        }
        if (us == 0) {
            continue; // sem superficie de operacao -> nao e alvo emitivel
        }
        if (!wantUid.empty()) {
            if (uidOf(b) == wantUid) {
                best = b;
                bestD2 = 0.0;
            }
            continue;
        }
        Ogre::Vector3 bp = b->getPosition();
        double dx = bp.x - wp.x, dy = bp.y - wp.y, dz = bp.z - wp.z;
        double d2 = dx * dx + dy * dy + dz * dz;
        if (best == 0 || d2 < bestD2) {
            best = b;
            bestD2 = d2;
        }
    }
    if (results.stuff != 0) { // so o array; os Building* seguem validos no tick
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
    return best;
}

} // namespace

void poc027TorreTick(GameWorld* world) {
    const core::PocEnvState& env = core::pocEnv();
    if (!env.turEnabled || world == 0) {
        return;
    }
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return;
    }
    if (world->player == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    ++g_round;
    bool throttle = (g_round - g_lastWaitLog) >= 6;

    if (env.worker.empty()) {
        if (throttle) {
            diag::log("TUR: LS_POC_WORKER nao definido -- defina o nome EXATO do "
                      "guarda. Nada a fazer (degraded-safe).");
            g_lastWaitLog = g_round;
        }
        return;
    }
    Character* w = findCharByName(world, env.worker);
    if (w == 0) {
        if (throttle) {
            diag::log("TUR: worker \"" + env.worker + "\" nao encontrado no roster.");
            g_lastWaitLog = g_round;
        }
        return;
    }
    TownBase* town = w->getCurrentTownLocation();
    if (town == 0) {
        if (throttle) {
            diag::log("TUR: worker fora de qualquer town -- leve-o para a base.");
            g_lastWaitLog = g_round;
        }
        return;
    }

    // ---- Lado LEITURA: censo de torres (lacunas 10/11) ----
    // Detalhado nas primeiras rodadas e na rodada da emissao; depois so contagem.
    bool detailed = (g_censusBudget > 0);
    if (detailed) {
        --g_censusBudget;
        diag::log("TUR censo de torres da base (lacunas 10/11):");
    }
    // Apos a emissao, o lookup TRAVA na torre alvo (g_turretUid) -- "mais
    // proxima" mudaria se o worker andasse e falsearia a observacao de ops.
    std::string lookupUid = g_turretUid.empty() ? env.turretUid : g_turretUid;
    int turretsSeen = 0;
    Building* target = findTurrets(world, town, w, lookupUid,
                                   fence.threadsClear, detailed, turretsSeen);
    {
        std::ostringstream s;
        s << "TUR #" << g_round << ": torres na base = " << turretsSeen;
        if (target != 0) {
            s << " | alvo = " << uidOf(target)
              << (env.turretUid.empty() ? " (mais proxima)" : " (LS_POC_TURRET)");
        } else {
            s << " | SEM alvo emitivel";
        }
        diag::log(s.str());
    }

    // ---- Sessao de REVERSAO ----
    if (env.revert) {
        std::string wantUid = env.turretUid; // "" = qualquer cargo 234 do worker
        if (!g_revertDone) {
            if (!core::writeGateOpen(mode, fence)) {
                if (throttle) {
                    diag::log("TUR REVERT: aguardando a cerca de escrita abrir");
                    g_lastWaitLog = g_round;
                }
                return;
            }
            dumpPermajobs(w, "pre-revert");
            int before = w->getPermajobCount();
            int removed = 0;
            for (int k = 0; k < TUR_MAX_REMOVE; ++k) {
                int slot = findTurretSlot(w, wantUid, 0);
                if (slot < 0) {
                    break;
                }
                if (adapters::emitRemovePermajob(mode, fence, w, slot)
                        != adapters::EMIT_OK) {
                    break;
                }
                ++removed;
            }
            int after = w->getPermajobCount();
            bool gone = (findTurretSlot(w, wantUid, 0) < 0);
            std::ostringstream s;
            s << "TUR REVERT: removidos=" << removed << " cargos:" << before
              << "->" << after << " | cargo 234 "
              << (gone ? "AUSENTE (CONFIRM-TUR-5 ok ate aqui)"
                       : "AINDA PRESENTE (re-tentando)");
            diag::milestone(s.str());
            if (gone) {
                g_revertDone = true;
            }
            return;
        }
        std::ostringstream s;
        s << "TUR pos-revert: cargos=" << w->getPermajobCount()
          << " acao_key=" << currentActionKey(w);
        diag::log(s.str());
        return;
    }

    // ---- Sessao NORMAL: emitir UMA vez ----
    if (g_phase == TUR_ARMED) {
        if (!core::writeGateOpen(mode, fence)) {
            if (throttle) {
                diag::log("TUR ARMADO: aguardando a cerca de escrita abrir");
                g_lastWaitLog = g_round;
            }
            return;
        }
        if (!eligibleTarget(w)) {
            if (throttle) {
                diag::log("TUR ARMADO: worker inelegivel (morto/KO/sem-ordens).");
                g_lastWaitLog = g_round;
            }
            return;
        }
        // Idempotencia entre SESSOES: cargo 234 ja no worker (save anterior)?
        // Nao re-emitir (fixedTarget=1 dedup por torre protegeria contra o
        // MESMO alvo, mas "mais proxima" pode variar -> criaria 2a torre).
        {
            bool saw146 = false;
            int existing = findTurretSlot(w, env.turretUid, &saw146);
            if (existing >= 0) {
                std::ostringstream s;
                s << "TUR: \"" << env.worker << "\" JA tem cargo 234 no slot "
                  << existing << " -- nada a emitir (sessao anterior); observando.";
                diag::milestone(s.str());
                g_turretUid = env.turretUid; // "" = observacao generica
                g_baseline = w->getPermajobCount();
                g_phase = TUR_OBSERVING;
                return;
            }
        }
        if (target == 0) {
            if (throttle) {
                diag::log("TUR ARMADO: nenhuma torre-alvo na base do worker "
                          "(getBuildingClass==BCTYPE_TURRET) -- construa/repare "
                          "uma torre ou confira LS_POC_TURRET.");
                g_lastWaitLog = g_round;
            }
            return;
        }
        UseableStuff* us = target->getUseableStuff();
        if (us == 0) {
            return; // ja filtrado; guarda dupla
        }
        // Verbo = o que a TORRE declara (TurretBuilding::getDefaultTask, TUR-09),
        // VALIDADO contra a constante nomeada USE_TURRET (sec.0.2). Divergiu?
        // Nao emitimos NADA (degraded-safe) e registramos a anomalia.
        TaskType dt = us->getDefaultTask();
        if (dt != USE_TURRET) {
            std::ostringstream s;
            s << "TUR ANOMALIA: getDefaultTask da torre " << uidOf(target)
              << " = " << static_cast<int>(dt) << " != USE_TURRET -- receita "
              << "TUR-09 refutada p/ esta torre; NAO emitindo (degraded-safe).";
            diag::milestone(s.str());
            g_phase = TUR_DONE;
            return;
        }
        // Aviso (nao-bloqueante): sem vaga p/ a mao do worker. O permajob ainda
        // pode ser criado (a execucao e que respeita slots); registrar p/ o log.
        {
            hand wh(w);
            if (!us->isFreeSlot(wh)) {
                diag::log("TUR aviso: isFreeSlot=false p/ o worker nesta torre "
                          "(opsMax cheio?) -- emitindo mesmo assim; o cargo e "
                          "duravel e a execucao respeita a vaga.");
            }
        }

        g_turretUid = uidOf(target);
        g_baseline = w->getPermajobCount();
        Ogre::Vector3 tp = target->getPosition();
        adapters::EmitResult r = adapters::emitAddPermajob(
            mode, fence, w, target, dt /*==USE_TURRET, validado acima*/, tp);

        int after = w->getPermajobCount();
        bool saw146 = false;
        int slot234 = findTurretSlot(w, g_turretUid, &saw146);
        {
            std::ostringstream s;
            s << "TUR EMITIU: worker=\"" << env.worker << "\" torre=" << g_turretUid
              << " task=USE_TURRET -> " << adapters::emitResultName(r)
              << " cargos:" << g_baseline << "->" << after
              << " slot_234=" << slot234 << (saw146 ? " [ANOMALIA: 146 na lista]" : "");
            diag::milestone(s.str());
        }
        dumpPermajobs(w, "pos-emit");

        if (r != adapters::EMIT_OK) {
            return; // re-arma na proxima rodada
        }
        if (after > g_baseline && slot234 >= 0 && !saw146) {
            diag::milestone("TUR CONFIRM-TUR-1: CONFIRMADO -- swap 146->234 "
                            "aconteceu (key 234 na lista, subject=torre). Em PAZ "
                            "o guarda pode fazer outra coisa (I-24, NAO e falha); "
                            "provoque um alerta p/ CONFIRM-TUR-3.");
        } else if (saw146) {
            diag::milestone("TUR CONFIRM-TUR-1: ANOMALIA -- USE_TURRET(146) fixado "
                            "CRU (sem swap p/ 234); registrar contra o mapa sec.1.2.");
        } else {
            diag::milestone("TUR CONFIRM-TUR-1: NAO confirmado (count/slot nao "
                            "refletem) -- ANOMALIA; observando.");
        }
        g_phase = TUR_OBSERVING; // single-shot
        return;
    }

    // ---- OBSERVANDO: lista (I-24) + execucao (currentOperators) ----
    if (g_phase == TUR_OBSERVING) {
        bool saw146 = false;
        int slot234 = findTurretSlot(w, g_turretUid, &saw146);
        bool inOps = false;
        bool opsObserved = false;
        if (fence.threadsClear && target != 0 && uidOf(target) == g_turretUid) {
            UseableStuff* us = target->getUseableStuff();
            if (us != 0) {
                inOps = setHasChar(us->currentOperators, w);
                opsObserved = true;
            }
        }
        std::ostringstream s;
        s << "TUR OBS: cargos=" << w->getPermajobCount() << " (baseline "
          << g_baseline << ") slot_234=" << slot234
          << " acao_key=" << currentActionKey(w);
        if (opsObserved) {
            s << (inOps ? " | NA TORRE (operando agora)"
                        : " | fora da torre (paz? I-24: ok se em paz)");
        } else {
            s << " | (ops nao-obs)";
        }
        if (slot234 < 0) {
            s << " | ATENCAO: cargo 234 sumiu da lista!";
        }
        if (saw146) {
            s << " | [146 na lista: anomalia]";
        }
        diag::log(s.str());
        return;
    }
}

} // namespace pocs
} // namespace ls
