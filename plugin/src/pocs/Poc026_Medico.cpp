// Living Settlements -- pocs/Poc026_Medico.cpp
// POC-MED-1. ASCII-only. So simbolos verificados no header sweep (sec.0.2):
//   Character::addJob(TaskType, RootObject*, bool, bool, Vector3&)  Character.h:416
//   Character::getPermajob*/removePermajob                          Character.h:418-424
//   Character::getMedical()                                         Character.h:533
//   MedicalSystem::needsFirstAidScoreTotal_robot@0xA8/_fleshy@0xAC  MedicalSystem.h:223-224
//   Tasker::subject@0x10, Tasker::taskData@0x70 -> TaskData::key@0x44 (Tasker.h)
// Escrita APENAS via OrderEmitter (choke unico) atras de writeGateOpen.
// GUARDRAILS: nenhum laco sobre contagem nativa sem cap duro.
//
// CRITERIOS DE CONFIRM (escritos ANTES da sessao; protocolo Fase A):
//   CONFIRM-MED-1 (criacao): getPermajobCount sobe +1 no mesmo tick E aparece
//     slot com key==JOB_MEDIC (58, constante nomeada) e subject nulo.
//   CONFIRM-MED-2 (execucao): com ferido na base + kit no inventario do medico,
//     ele trata SOZINHO (observacao; acao corrente key==JOB_MEDIC/FIRST_AID_ORDER
//     no log). SEM kit: cargo existe e nao executa -- NAO e falha (I-25).
//   CONFIRM-MED-3 (escopo do finder, lacuna 2 do mapa sec.9): ferir char de
//     OUTRO squad e observar se atende (censo de feridos no log da POC).
//   CONFIRM-MED-4 (durabilidade+reversao, sessao 2 com LS_POC_REVERT=1): cargo
//     presente apos save/load; removePermajob(slot) -> count desce, slot some,
//     char volta ao GOAP puro.
#include "pocs/Poc026_Medico.h"
#include "core/PocEnv.h"
#include "core/Diagnostics.h"
#include "core/LifecycleGate.h"
#include "core/LsConfig.h"
#include "adapters/OrderEmitter.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharBody.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/GameData.h>
#include <kenshi/Tasker.h>
#include <kenshi/Town.h>
#include <kenshi/InstanceID.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/Enums.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdlib>   // free()

#include <cstdint>
#include <string>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Caps duros (guardrail do hang).
static const uint32_t MED_MAX_CHARS       = 512; // roster do jogador
static const int      MED_MAX_SLOTS       = 64;  // slots de permajob varridos
static const int      MED_MAX_REMOVE      = 8;   // remocoes na sessao de reversao
static const int      MED_WOUNDED_DETAIL  = 12;  // linhas de detalhe de feridos/rodada
static const int      MED_KIT_LIST        = 20;  // nomes listados no scan de kits

enum MedPhase { MED_ARMED = 0, MED_OBSERVING, MED_DONE };
MedPhase      g_phase = MED_ARMED;
unsigned long g_round = 0;
unsigned long g_lastWaitLog = 0;
int           g_baseline = -1;
bool          g_revertDone = false;
bool          g_kitScanDone = false;
bool          g_storageScanDone = false;

Character* findCharByName(GameWorld* world, const std::string& name) {
    if (world == 0 || world->player == 0 || name.empty()) {
        return 0;
    }
    lektor<Character*>& chars = world->player->playerCharacters;
    uint32_t n = chars.size();
    if (n > MED_MAX_CHARS) {
        n = MED_MAX_CHARS;
    }
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c != 0 && c->getName() == name) {
            return c;
        }
    }
    return 0;
}

// Slot do cargo JOB_MEDIC na lista de permajobs (dedup nativo garante max 1:
// fixedTarget=0, MED-05). -1 = ausente. Cap duro no scan.
int findMedicSlot(Character* w) {
    int n = w->getPermajobCount();
    if (n > MED_MAX_SLOTS) {
        n = MED_MAX_SLOTS;
    }
    for (int s = 0; s < n; ++s) {
        if (w->getPermajob(s) == JOB_MEDIC) {
            return s;
        }
    }
    return -1;
}

// Dump completo dos permajobs (mesma leitura provada do AIPROBE/H11).
void dumpPermajobs(Character* w, const char* tag) {
    int n = w->getPermajobCount();
    {
        std::ostringstream h;
        h << "MED DUMP[" << tag << "] \"" << w->getName()
          << "\": permajobCount=" << n;
        diag::log(h.str());
    }
    if (n > MED_MAX_SLOTS) {
        n = MED_MAX_SLOTS;
    }
    for (int s = 0; s < n; ++s) {
        TaskType k = w->getPermajob(s);
        std::string nm = w->getPermajobName(s);
        const Tasker* tk = w->getPermajobData(s);
        bool subjValid = false;
        if (tk != 0) {
            subjValid = tk->subject.isValid();
        }
        std::ostringstream s2;
        s2 << "    [" << s << "] key=" << static_cast<int>(k) << " \"" << nm
           << "\" subj=" << (subjValid ? "sim" : "(nulo)");
        diag::log(s2.str());
    }
}

// Censo de feridos do roster (lado LEITURA da POC; alimenta CONFIRM-MED-2/3).
// Os floats precalculados sao escritos em fase THREADED -> SO com filas limpas
// (inv.21); stale-tolerante. Retorna quantos feridos viu (-1 = nao observado).
int woundedCensus(GameWorld* world, bool threadsClear) {
    if (!threadsClear) {
        return -1;
    }
    lektor<Character*>& chars = world->player->playerCharacters;
    uint32_t n = chars.size();
    if (n > MED_MAX_CHARS) {
        n = MED_MAX_CHARS;
    }
    int wounded = 0;
    int detail = 0;
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c == 0) {
            continue;
        }
        MedicalSystem* med = c->getMedical();
        if (med == 0) {
            continue;
        }
        float fleshy = med->needsFirstAidScoreTotal_fleshy;
        float robot = med->needsFirstAidScoreTotal_robot;
        if (fleshy > 0.0f || robot > 0.0f) {
            ++wounded;
            if (detail < MED_WOUNDED_DETAIL) {
                std::ostringstream s;
                s << "    ferido: \"" << c->getName() << "\" fleshy=" << fleshy
                  << " robot=" << robot;
                diag::log(s.str());
                ++detail;
            }
        }
    }
    return wounded;
}

// Acao corrente do worker (leitura ja provada: SnapshotBuilder/Poc023). Casa
// a key do Tasker corrente com o TaskType (mapa sec.6.4: taskData@0x70->key@0x44,
// leitura por MEMBRO nomeado do header, sem call). -1 = sem acao legivel.
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

bool eligibleTarget(Character* c) {
    return c != 0 && c->isAnimal() == 0 && !c->isDead() && !c->isUnconcious()
        && c->canTakePlayerOrdersAtThisTime();
}

// Kit de primeiros socorros no inventario do worker: 1/0; -1 = nao observado.
// Inventario e mutado em worker thread (mesma familia do peso) -> SO com as
// filas limpas (inv.21). hasItemFunction [V] Inventory.h:195; ITEM_FIRSTAID
// [V] Enums.h:227. Kit e requisito de EXECUCAO, nao de criacao (I-25).
int workerKit(Character* w, bool threadsClear) {
    if (!threadsClear) {
        return -1;
    }
    Inventory* inv = w->getInventory();
    if (inv == 0) {
        return -1;
    }
    return inv->hasItemFunction(ITEM_FIRSTAID) ? 1 : 0;
}

const char* kitLabel(int kit) {
    return (kit < 0) ? "(nao-obs)" : (kit > 0 ? "sim" : "NAO");
}

// PROCEDENCIA de um item: stringID do GameData ("1866-gamedata.base") -- o
// sufixo e o ARQUIVO DE ORIGEM (vanilla = gamedata.base; mod = NomeDoMod.mod).
// data e membro publico da raiz (RootObjectBase.h:76); stringID GameData.h:92.
// Base da camada de compatibilidade com mods: classificar por FUNCAO e
// rotular por ORIGEM, nunca por nome de item.
std::string itemProvenance(Item* it) {
    if (it == 0) {
        return std::string();
    }
    GameData* gd = it->data;
    if (gd == 0) {
        return std::string();
    }
    return gd->stringID;
}

// One-shot (1a rodada thread-safe): QUEM no roster carrega kit E -- o
// ORACULO -- o NOME exato do item que o MOTOR aceita como kit (o melhor
// ITEM_FIRSTAID do primeiro portador). Com 57 data-mods, nome de item a
// olho nu nao e confiavel ("bandagem" sem funcao); o motor e o juiz.
void kitScanRoster(GameWorld* world) {
    lektor<Character*>& chars = world->player->playerCharacters;
    uint32_t n = chars.size();
    if (n > MED_MAX_CHARS) {
        n = MED_MAX_CHARS;
    }
    int withKit = 0, listed = 0;
    std::string oracle, oracleProv;
    std::ostringstream names;
    for (uint32_t i = 0; i < n; ++i) {
        Character* c = chars[i];
        if (c == 0 || c->isAnimal() != 0) {
            continue;
        }
        Inventory* inv = c->getInventory();
        if (inv == 0 || !inv->hasItemFunction(ITEM_FIRSTAID)) {
            continue;
        }
        ++withKit;
        if (oracle.empty()) {
            Item* best = inv->getBestItemWithFunction(ITEM_FIRSTAID);
            if (best != 0) {
                oracle = best->getName(); // getName herdado (RootObjectBase.h:32)
                oracleProv = itemProvenance(best);
            }
        }
        if (listed < MED_KIT_LIST) {
            if (listed > 0) {
                names << ", ";
            }
            names << "\"" << c->getName() << "\"";
            ++listed;
        }
    }
    std::ostringstream s;
    s << "MED kits no roster: " << withKit << " com kit de primeiros socorros";
    if (withKit > 0) {
        s << ": " << names.str();
        if (withKit > listed) {
            s << " (+" << (withKit - listed) << ")";
        }
    }
    s << ". Kit e requisito de EXECUCAO (I-25).";
    diag::milestone(s.str());
    if (!oracle.empty()) {
        diag::milestone("MED ORACULO: o item que o motor aceita como kit no SEU "
                        "jogo chama-se \"" + oracle + "\" [" + oracleProv + "] "
                        "-- e ESTE item que o medico precisa ter no inventario "
                        "(sufixo do id = mod de origem).");
    }
}

// One-shot (thread-safe): DEPOSITOS da base com kit valido (funcao
// ITEM_FIRSTAID no inventario do predio). Responde "tem kit no storage?"
// pelo criterio do motor e prepara a lacuna da busca automatica. Caps:
// LS_M0_MAX_RESULTS predios, 8 linhas de log.
void kitScanStorages(GameWorld* world, TownBase* town) {
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
    int hits = 0, listed = 0;
    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* o = results[i];
        if (o == 0) {
            continue;
        }
        Building* b = static_cast<Building*>(o);
        if (b->getTown() != town) {
            continue;
        }
        UseableStuff* us = b->getUseableStuff();
        if (us == 0) {
            continue;
        }
        Inventory* inv = us->getInventory();
        if (inv == 0 || !inv->hasItemFunction(ITEM_FIRSTAID)) {
            continue;
        }
        ++hits;
        if (listed < 8) {
            std::string item, prov;
            Item* best = inv->getBestItemWithFunction(ITEM_FIRSTAID);
            if (best != 0) {
                item = best->getName();
                prov = itemProvenance(best);
            }
            InstanceID* iid = b->getInstanceID();
            std::ostringstream s;
            s << "    deposito c/ kit valido: " << (iid != 0 ? iid->uid : std::string("?"))
              << " (\"" << b->getName() << "\") item=\"" << item << "\" [" << prov << "]";
            diag::log(s.str());
            ++listed;
        }
    }
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
    std::ostringstream s;
    s << "MED depositos: " << hits << " predio(s) da base com kit VALIDO no "
      << "inventario (criterio do motor).";
    diag::milestone(s.str());
}

} // namespace

void poc026MedicoTick(GameWorld* world) {
    const core::PocEnvState& env = core::pocEnv();
    if (!env.medEnabled || world == 0) {
        return;
    }
    // Switch de escrita PROPRIO da POC (o coordenador geral segue sombra):
    // modo avaliado com writesEnabled=true; toda acao atras da cerca completa.
    core::CoordMode mode = core::evaluateLifecycle(world, true);
    if (mode != core::MODE_OBSERVE_AND_ACT) {
        return; // load/reset/mundo ausente -> nem ler
    }
    if (world->player == 0) {
        return;
    }
    core::WriteFence fence = core::evaluateWriteFence(world);
    ++g_round;
    bool throttle = (g_round - g_lastWaitLog) >= 6;

    // ---- Lado LEITURA (toda rodada): censo de feridos da base ----
    int wounded = woundedCensus(world, fence.threadsClear);
    {
        std::ostringstream s;
        s << "MED #" << g_round << ": feridos no roster = ";
        if (wounded < 0) {
            s << "(nao-obs: filas de thread nao-limpas, inv.21)";
        } else {
            s << wounded;
        }
        diag::log(s.str());
    }
    // One-shot: quem tem kit (orienta a escolha/checagem do worker).
    if (!g_kitScanDone && fence.threadsClear) {
        kitScanRoster(world);
        g_kitScanDone = true;
    }

    // ---- Worker alvo (LS_POC_MED_WORKER, cai em LS_POC_WORKER; nome exato) ----
    if (env.medWorker.empty()) {
        if (throttle) {
            diag::log("MED: LS_POC_MED_WORKER/LS_POC_WORKER nao definido -- defina "
                      "o nome EXATO do medico. Nada a fazer (degraded-safe).");
            g_lastWaitLog = g_round;
        }
        return;
    }
    Character* w = findCharByName(world, env.medWorker);
    if (w == 0) {
        if (throttle) {
            diag::log("MED: worker \"" + env.medWorker + "\" nao encontrado no "
                      "roster -- confira o nome (exato, com maiusculas).");
            g_lastWaitLog = g_round;
        }
        return;
    }
    // One-shot: depositos da base com kit valido (oraculo do storage).
    if (!g_storageScanDone && fence.threadsClear) {
        TownBase* town = w->getCurrentTownLocation();
        if (town != 0) {
            kitScanStorages(world, town);
            g_storageScanDone = true;
        }
    }

    // ---- Sessao de REVERSAO (LS_POC_REVERT=1): remover em vez de criar ----
    if (env.revert) {
        if (!g_revertDone) {
            if (!core::writeGateOpen(mode, fence)) {
                if (throttle) {
                    diag::log("MED REVERT: aguardando a cerca de escrita abrir");
                    g_lastWaitLog = g_round;
                }
                return;
            }
            dumpPermajobs(w, "pre-revert");
            int before = w->getPermajobCount();
            int removed = 0;
            // JOB_MEDIC tem fixedTarget=0 -> max 1 slot; o laco com cap cobre o
            // caso anomalo. Remocao por indice re-consultado apos cada remocao.
            for (int k = 0; k < MED_MAX_REMOVE; ++k) {
                int slot = findMedicSlot(w);
                if (slot < 0) {
                    break;
                }
                if (adapters::emitRemovePermajob(mode, fence, w, slot)
                        != adapters::EMIT_OK) {
                    break; // cerca fechou -> re-tenta na proxima rodada
                }
                ++removed;
            }
            int after = w->getPermajobCount();
            bool gone = (findMedicSlot(w) < 0);
            std::ostringstream s;
            s << "MED REVERT: removidos=" << removed << " cargos:" << before
              << "->" << after << " | JOB_MEDIC "
              << (gone ? "AUSENTE (CONFIRM-MED-4 ok ate aqui; observe o char "
                         "voltar ao GOAP puro)"
                       : "AINDA PRESENTE (re-tentando na proxima rodada)");
            diag::milestone(s.str());
            if (gone) {
                g_revertDone = true;
            }
            return;
        }
        // Pos-reversao: acompanhar o retorno ao GOAP (leve, toda rodada).
        std::ostringstream s;
        s << "MED pos-revert: cargos=" << w->getPermajobCount()
          << " acao_key=" << currentActionKey(w);
        diag::log(s.str());
        return;
    }

    // ---- Sessao NORMAL: emitir UMA vez e observar ----
    if (g_phase == MED_ARMED) {
        if (!core::writeGateOpen(mode, fence)) {
            if (throttle) {
                diag::log("MED ARMADO: aguardando a cerca de escrita abrir "
                          "(save/load/filas)");
                g_lastWaitLog = g_round;
            }
            return;
        }
        if (!eligibleTarget(w)) {
            if (throttle) {
                diag::log("MED ARMADO: worker \"" + env.medWorker + "\" inelegivel "
                          "(morto/KO/sem-ordens) -- aguardando.");
                g_lastWaitLog = g_round;
            }
            return;
        }
        // Idempotencia ANTES de emitir (dedup nativo MED-05 tornaria no-op,
        // mas degraded-safe = nem emitir): ja tem JOB_MEDIC? So observar.
        int existing = findMedicSlot(w);
        if (existing >= 0) {
            std::ostringstream s;
            s << "MED: \"" << env.medWorker << "\" JA tem JOB_MEDIC no slot "
              << existing << " -- nada a emitir (idempotente); observando.";
            diag::milestone(s.str());
            g_baseline = w->getPermajobCount();
            g_phase = MED_OBSERVING;
            return;
        }

        g_baseline = w->getPermajobCount();
        // RECEITA POC-MED-1 (mapa-papeis sec.3, verificada + decisao 2 do dono):
        // JOB_MEDIC por constante nomeada; subject NULL (o finder nativo acha os
        // pacientes -- e o que o botao Medic passa); pos = a do proprio worker.
        Ogre::Vector3 wp = w->getPosition();
        adapters::EmitResult r = adapters::emitAddPermajob(
            mode, fence, w, 0 /*subject NULL, valido p/ JOB_MEDIC (MED-13)*/,
            JOB_MEDIC, wp);

        int after = w->getPermajobCount();
        int slot = findMedicSlot(w);
        {
            std::ostringstream s;
            s << "MED EMITIU: worker=\"" << env.medWorker << "\" task=JOB_MEDIC -> "
              << adapters::emitResultName(r) << " cargos:" << g_baseline << "->"
              << after << " slot_58=" << slot
              << " kit=" << kitLabel(workerKit(w, fence.threadsClear));
            diag::milestone(s.str());
        }
        dumpPermajobs(w, "pos-emit");

        if (r != adapters::EMIT_OK) {
            return; // bloqueado (cerca/autoridade) -> re-arma na proxima rodada
        }
        if (after > g_baseline && slot >= 0) {
            diag::milestone("MED CONFIRM-MED-1: CONFIRMADO -- permajob JOB_MEDIC "
                            "criado (count subiu, key na lista). Agora: ferir um "
                            "char + kit no inventario do medico (CONFIRM-MED-2).");
        } else {
            diag::milestone("MED CONFIRM-MED-1: NAO confirmado (EMIT_OK mas count/"
                            "slot nao refletem) -- ANOMALIA; observando.");
        }
        g_phase = MED_OBSERVING; // single-shot: nunca re-emite nesta sessao
        return;
    }

    // ---- OBSERVANDO: presenca do cargo + acao corrente, toda rodada ----
    if (g_phase == MED_OBSERVING) {
        int slot = findMedicSlot(w);
        std::ostringstream s;
        s << "MED OBS: cargos=" << w->getPermajobCount() << " (baseline "
          << g_baseline << ") slot_58=" << slot
          << " acao_key=" << currentActionKey(w)
          << " kit=" << kitLabel(workerKit(w, fence.threadsClear))
          << (slot >= 0 ? "" : " | ATENCAO: JOB_MEDIC sumiu da lista!");
        diag::log(s.str());
        return;
    }
}

} // namespace pocs
} // namespace ls
