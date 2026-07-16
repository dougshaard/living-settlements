// Living Settlements — core/TickHost.cpp
#include "core/TickHost.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"
#include "pocs/Poc001_EnumerarPersonagens.h"
#include "pocs/Poc002_EnumerarBase.h"
#include "pocs/Poc003_DetectarAmeaca.h"
#include "pocs/Poc011_EmitirOrdem.h"
#include "pocs/Poc020_RetratoTrabalho.h"
#include "pocs/Poc021_LifecycleProbe.h"
#include "pocs/Poc022_H11.h"
#include "pocs/Poc023_AITaskProbe.h"
#include "pocs/Poc025_Organizador.h"
#include "pocs/Poc026_Medico.h"
#include "pocs/Poc027_Torre.h"
#include "core/PocEnv.h"

#include <core/Functions.h>   // KenshiLib::AddHook / GetRealAddress
#include <kenshi/GameWorld.h> // GameWorld::_NV_mainLoop_GPUSensitiveStuff, isPaused
#include <kenshi/ModInfo.h>   // censo de mods ativos (compat por origem)

#include <sstream>

namespace ls {
namespace core {

namespace {

// Trampolim original do mainLoop (preenchido por AddHook).
void (*g_mainLoopOrig)(GameWorld* thisptr, float time) = 0;

// Acumulador de tempo entre rodadas de POC. Estado tocado apenas
// pela main thread (ADR-014).
float g_accumSeconds = 0.0f;
unsigned long g_tickCount = 0;
unsigned long g_lastRoundTick = 0;
unsigned long g_pocRound = 0;

// A UNIDADE do parametro 'time' do mainLoop NAO e documentada em nenhum
// header/exemplo (suposicao: segundos reais). Guarda de sanidade:
// exigir tambem um minimo de ticks entre rodadas, e logar o acumulado
// observado — o timestamp [HH:MM:SS] do log valida ou refuta a
// hipotese de unidade no primeiro run.
static const unsigned long LS_MIN_TICKS_BETWEEN_ROUNDS = 60;

// Censo de MODS ativos, one-shot por sessao (Fase A; base da camada de
// compatibilidade): nome + arquivo de cada mod carregado. O arquivo casa com
// o sufixo do stringID de todo GameData -> da a ORIGEM de qualquer item/
// predio ("de qual mod veio"). Leitura de config (lista montada no boot do
// jogo, nao mutada em worker thread). Cap duro de linhas.
bool g_modCensusDone = false;

void logModCensus(GameWorld* world) {
    lektor<ModInfo*>& mods = world->activeMods;
    uint32_t n = mods.size();
    std::ostringstream h;
    h << "MODS ativos: " << n << " (arquivo do mod = sufixo do stringID de "
      << "todo item/predio; e a chave de compatibilidade por origem)";
    diag::milestone(h.str());
    if (n > 128) {
        n = 128;
    }
    for (uint32_t i = 0; i < n; ++i) {
        ModInfo* m = mods[i];
        if (m == 0) {
            continue;
        }
        std::ostringstream s;
        s << "    mod[" << i << "] \"" << m->name << "\" arquivo=" << m->file
          << (m->isBaseMod ? " [base]" : "")
          << (m->isWorkshop ? " [workshop]" : "");
        diag::log(s.str());
    }
}

void runPocRound(GameWorld* world, float accumulated) {
    ++g_pocRound;
    std::ostringstream head;
    head << "---- rodada de POCs #" << g_pocRound
         << " (tick " << g_tickCount
         << ", acumulado=" << accumulated
         << " 'time-units' em " << (g_tickCount - g_lastRoundTick)
         << " ticks) ----";
    g_lastRoundTick = g_tickCount;
    diag::log(head.str());

    // try/catch cobre exceções C++ do nosso próprio código (std::bad_alloc
    // etc). NÃO captura access violation do jogo (isso exigiria /EHa);
    // um AV aqui é sinal de premissa errada sobre a API e DEVE derrubar
    // cedo na prova técnica, não ser engolido (PRINC-006).
    if (LS_ENABLE_POC001) {
        try {
            pocs::poc001Run(world);
        } catch (...) {
            diag::error("POC-001 lancou excecao C++ -- POC-001 abortada; "
                        "seguindo para a proxima POC");
        }
    }
    if (LS_ENABLE_POC002) {
        try {
            pocs::poc002Run(world);
        } catch (...) {
            diag::error("POC-002 lancou excecao C++ -- POC-002 abortada");
        }
    }
    if (LS_ENABLE_POC003) {
        try {
            pocs::poc003Run(world);
        } catch (...) {
            diag::error("POC-003 lancou excecao C++ -- POC-003 abortada");
        }
    }
    if (LS_ENABLE_POC020) {
        try {
            pocs::poc020Run(world);
        } catch (...) {
            diag::error("POC-020 (Marco 0) lancou excecao C++ -- abortada");
        }
    }
    if (LS_ENABLE_H11) {
        try {
            pocs::poc022H11Tick(world);
        } catch (...) {
            diag::error("POC-H11 (1a escrita) lancou excecao C++ -- abortada");
        }
    }
    if (LS_ENABLE_AIPROBE) {
        try {
            pocs::poc023AiProbeTick(world);
        } catch (...) {
            diag::error("AIPROBE (read-only) lancou excecao C++ -- abortada");
        }
    }
    if (LS_ENABLE_ORGANIZER) {
        try {
            pocs::poc025OrganizadorTick(world);
        } catch (...) {
            diag::error("ORGANIZADOR lancou excecao C++ -- abortado");
        }
    }
    // Fase A: POCs por toggles (default OFF; ver core/PocEnv.h). A checagem
    // fina (flag + worker + cerca) vive dentro de cada POC.
    if (!g_modCensusDone && (pocEnv().medEnabled || pocEnv().turEnabled)) {
        try {
            logModCensus(world);
        } catch (...) {
            diag::error("censo de mods lancou excecao C++ -- ignorado");
        }
        g_modCensusDone = true;
    }
    if (pocEnv().medEnabled) {
        try {
            pocs::poc026MedicoTick(world);
        } catch (...) {
            diag::error("POC-MED-1 lancou excecao C++ -- abortada");
        }
    }
    if (pocEnv().turEnabled) {
        try {
            pocs::poc027TorreTick(world);
        } catch (...) {
            diag::error("POC-TUR-1 lancou excecao C++ -- abortada");
        }
    }
    if (LS_ENABLE_POC011) {
        try {
            pocs::poc011Tick(world);
        } catch (...) {
            diag::error("POC-011 lancou excecao C++ -- POC-011 abortada");
        }
    }

    diag::flush();
}

// Detour do mainLoop. Contrato (ADR-014): estamos na main thread do jogo;
// fazemos nosso trabalho e devolvemos o controle ao loop original
// (mesmo padrão do exemplo CharacterHighlight).
void mainLoopHook(GameWorld* thisptr, float time) {
    ++g_tickCount;

    // Probe de ciclo de vida (M2): TODO tick, ANTES do guard de pausa/
    // throttle -- e o unico jeito de observar os sinais durante um save/
    // load/reset (que podem nao coincidir com uma rodada de POC). So le
    // flags primitivos e edge-loga; nunca pode derrubar o tick.
    if (LS_ENABLE_POC021 && thisptr != 0) {
        try {
            pocs::poc021Probe(thisptr);
        } catch (...) {
            diag::error("POC-021 (probe M2) lancou excecao C++ -- ignorada");
        }
    }

    // Guardas: mundo ausente ou pausado => nada a observar neste tick.
    // (isPaused: o jogador pausou; não gastamos orçamento de POC.)
    if (thisptr != 0 && !thisptr->isPaused()) {
        g_accumSeconds += time;
        if (g_accumSeconds >= LS_POC_INTERVAL_SECONDS
            && (g_tickCount - g_lastRoundTick) >= LS_MIN_TICKS_BETWEEN_ROUNDS) {
            float accumulated = g_accumSeconds;
            g_accumSeconds = 0.0f;
            runPocRound(thisptr, accumulated);
        }
    }

    g_mainLoopOrig(thisptr, time);
}

} // namespace

bool installTickHook() {
    // Variante _NV_ obrigatória: GetRealAddress não resolve virtuais
    // (core/Functions.h). Padrão idêntico ao exemplo oficial.
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
            &mainLoopHook,
            &g_mainLoopOrig)) {
        diag::error("falha ao instalar o hook do tick (mainLoop_GPUSensitiveStuff)");
        return false;
    }
    diag::milestone("hook do tick instalado (main thread, ADR-014)");
    return true;
}

} // namespace core
} // namespace ls
