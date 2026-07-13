// Living Settlements -- pocs/Poc021_LifecycleProbe.cpp
// Marco 2: edge-log dos sinais de EVENTO (load/reset/paused) + resumo da
// taxa de filas-limpas. ASCII-only.
//
// Achado do 1o run do probe: isLoadingFromASaveGame fica 1 durante toda a
// carga (com o jogo pausado) e volta a 0 ao entrar no jogo -> sinal limpo.
// Ja allThreadQueuesAreClear alterna dezenas de vezes/s -> NAO edge-logar
// (floodava o log e custava frame-time); vira um resumo por janela.
#include "pocs/Poc021_LifecycleProbe.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"
#include "core/LifecycleGate.h"

#include <kenshi/GameWorld.h>
#include <kenshi/SaveManager.h>
#include <kenshi/SaveFileSystem.h>

#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Estado da main thread (sem sincronizacao). Ultimos valores dos sinais
// de EVENTO (raros) -> so logamos quando algum muda.
bool g_have = false;
bool g_load = false;
bool g_reset = false;
bool g_paused = false;
unsigned long g_ticks = 0;

// Janela de amostragem da disponibilidade das filas de worker thread.
unsigned long g_winTicks = 0;
unsigned long g_winClear = 0;

// POC-SAVE-1 (H1-escrita): ultimos valores dos sinais de save (raros -> edge).
bool g_haveSave = false;
int  g_signal = 0;    // SaveManager::signal (0=nada; enum Signal{SAVEGAME=1,...})
int  g_saveState = 0; // SaveFileSystem::state (enum State{NORMAL,SAVING,COMPLETE})

// Janela do FENCE de escrita (parte 5): quanto tempo estaria aberto e por que
// fecha (filas vs save/load). Resumo por janela -- nao edge (filas flapam).
unsigned long g_wfWin = 0;
unsigned long g_wfOpen = 0;
unsigned long g_wfBlockSave = 0;
unsigned long g_wfBlockThread = 0;
unsigned long g_wfBlockUnavail = 0;  // singleton ausente (-1): nem save nem filas

} // namespace

void poc021Probe(GameWorld* world) {
    if (world == 0) {
        return;
    }
    ++g_ticks;

    // Sinais de gate (flags primitivos -- leitura mais segura que existe).
    bool load = world->isLoadingFromASaveGame();
    bool reset = world->gameResetting;
    bool paused = world->isPaused();
    bool clear = world->allThreadQueuesAreClear();

    // Filas: amostrar a taxa (nao edge-logar -- alterna rapido demais).
    ++g_winTicks;
    if (clear) {
        ++g_winClear;
    }

    if (!g_have) {
        std::ostringstream s;
        s << "M2 probe inicial (tick " << g_ticks << "): load=" << load
          << " reset=" << reset << " paused=" << paused
          << " filasLimpas=" << clear;
        diag::milestone(s.str());
        g_have = true;
        g_load = load; g_reset = reset; g_paused = paused;
        return;
    }

    // Edge SO nos sinais de evento (load/reset/paused). filasLimpas entra
    // como contexto do instante.
    if (load != g_load || reset != g_reset || paused != g_paused) {
        std::ostringstream s;
        s << "M2 EDGE (tick " << g_ticks << "):";
        if (load != g_load)     s << " load " << g_load << "->" << load << ";";
        if (reset != g_reset)   s << " reset " << g_reset << "->" << reset << ";";
        if (paused != g_paused) s << " paused " << g_paused << "->" << paused << ";";
        s << " (filasLimpas=" << clear << ")";
        diag::milestone(s.str());
        g_load = load; g_reset = reset; g_paused = paused;
    }

    // POC-SAVE-1 (H1-escrita): sinais de SAVE legiveis na main thread. Proxies
    // verificados -- SaveManager::signal (enum SAVEGAME/LOAD/IMPORT/NEWGAME) e
    // SaveFileSystem::state (NORMAL/SAVING/COMPLETE). Edge-log ao longo de
    // save/autosave prova a JANELA e a ORDEM de limpeza (signal vs state) ->
    // decide se a cerca de escrita fail-closed e hermetica (parte 5 secao 6).
    {
        SaveManager* sm = SaveManager::getSingleton();
        SaveFileSystem* sfs = SaveFileSystem::getSingleton();
        int sig = (sm != 0) ? sm->signal : -1;
        int sdelay = (sm != 0) ? sm->delay : -1;
        int sst = (sfs != 0) ? static_cast<int>(sfs->state) : -1;
        if (!g_haveSave) {
            std::ostringstream s;
            s << "M2-SAVE inicial (tick " << g_ticks << "): signal=" << sig
              << " delay=" << sdelay << " saveState=" << sst;
            diag::milestone(s.str());
            g_haveSave = true;
            g_signal = sig; g_saveState = sst;
        } else if (sig != g_signal || sst != g_saveState) {
            std::ostringstream s;
            s << "M2-SAVE EDGE (tick " << g_ticks << "):";
            if (sig != g_signal) {
                s << " signal " << g_signal << "->" << sig
                  << " (delay=" << sdelay << ");";
            }
            if (sst != g_saveState) {
                s << " saveState " << g_saveState << "->" << sst << ";";
            }
            diag::milestone(s.str());
            g_signal = sig; g_saveState = sst;
        }
    }

    // Estatistica do FENCE de escrita (parte 5 secao 6): quanto do tempo estaria
    // ABERTO e o que o fecha. Fonte unica: core::evaluateWriteFence.
    {
        core::WriteFence wf = core::evaluateWriteFence(world);
        ++g_wfWin;
        // -1 = singleton ausente (fail-closed). NAO e save/load ativo -- nao
        // contar como tal (senao a % "save/load" mente). signal/saveState > 0
        // sao os estados de operacao reais (SAVE=1.., SAVING=1/COMPLETE=2).
        bool unavailable = (wf.signal < 0) || (wf.saveState < 0);
        bool lifecycleActive = (wf.signal > 0) || (wf.saveState > 0)
                            || wf.loading || wf.resetting;
        if (wf.open) {
            ++g_wfOpen;
        } else if (lifecycleActive) {
            ++g_wfBlockSave;    // fechado por save/load/reset/import/newgame
        } else if (unavailable) {
            ++g_wfBlockUnavail; // fechado por singleton ausente (raro)
        } else {
            ++g_wfBlockThread;  // fechado so pelas filas (quiescencia)
        }
    }

    // Resumo periodico da disponibilidade das filas (para H_THREAD) + fence.
    if (g_winTicks >= static_cast<unsigned long>(LS_M2_SUMMARY_TICKS)) {
        std::ostringstream s;
        long pct = (g_winTicks > 0)
            ? static_cast<long>((g_winClear * 100) / g_winTicks) : 0;
        s << "M2 filas: limpas em " << g_winClear << " de " << g_winTicks
          << " ticks (" << pct << "%)";
        diag::log(s.str());

        std::ostringstream s2;
        long po = (g_wfWin > 0) ? static_cast<long>((g_wfOpen * 100) / g_wfWin) : 0;
        long ps = (g_wfWin > 0) ? static_cast<long>((g_wfBlockSave * 100) / g_wfWin) : 0;
        long pt = (g_wfWin > 0) ? static_cast<long>((g_wfBlockThread * 100) / g_wfWin) : 0;
        long pu = (g_wfWin > 0) ? static_cast<long>((g_wfBlockUnavail * 100) / g_wfWin) : 0;
        s2 << "M2 fence-escrita: aberta " << po << "% (bloqueio: filas " << pt
           << "%, save/load " << ps << "%, indisp " << pu << "%) em " << g_wfWin << " ticks";
        diag::log(s2.str());

        g_winTicks = 0;
        g_winClear = 0;
        g_wfWin = 0; g_wfOpen = 0; g_wfBlockSave = 0; g_wfBlockThread = 0;
        g_wfBlockUnavail = 0;
    }
}

} // namespace pocs
} // namespace ls
