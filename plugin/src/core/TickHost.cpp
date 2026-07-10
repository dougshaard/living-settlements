// Living Settlements — core/TickHost.cpp
#include "core/TickHost.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"
#include "pocs/Poc001_EnumerarPersonagens.h"
#include "pocs/Poc002_EnumerarBase.h"
#include "pocs/Poc003_DetectarAmeaca.h"
#include "pocs/Poc011_EmitirOrdem.h"

#include <core/Functions.h>   // KenshiLib::AddHook / GetRealAddress
#include <kenshi/GameWorld.h> // GameWorld::_NV_mainLoop_GPUSensitiveStuff, isPaused

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
