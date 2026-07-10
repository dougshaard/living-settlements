// Living Settlements — main.cpp
// Entry point do plugin RE_Kenshi/KenshiLib.
//
// Contrato do loader (RE_Kenshi/Plugins.cpp): o RE_Kenshi resolve o
// export MSVC-mangled "?startPlugin@@YAXXZ" — ou seja, a função DEVE
// ser exatamente `__declspec(dllexport) void startPlugin()` SEM
// extern "C" (o mangling faz parte do contrato).
//
// Sequência (fail-closed, ADR-011):
//   1. diagnóstico (POC-010) — sempre disponível, mesmo reprovado;
//   2. version gate (FACT-017) — reprovou => plugin vira no-op logado;
//   3. hook único do tick na main thread (ADR-014).
//
// AVISO de ciclo de vida: startPlugin roda cedo no processo; o global
// `ou` (Globals.h) pode ainda ser nulo aqui. NENHUM estado de jogo é
// tocado neste ponto — só dentro do tick hook (padrão dos exemplos).
#include "core/LsConfig.h"
#include "core/Diagnostics.h"
#include "core/VersionGate.h"
#include "core/TickHost.h"

#include <string>

__declspec(dllexport) void startPlugin() {
    std::string banner = std::string(ls::LS_NAME) + " " + ls::LS_VERSION
        + " (Etapa 0 -- prova tecnica, POC-001/002/010)";
    ls::diag::init(banner);

    if (!ls::core::versionGateAllowsHooks()) {
        // Fail-closed: sem hooks; o log registra o motivo (REQ-CORE-008).
        return;
    }

    if (!ls::core::installTickHook()) {
        // Degradação graciosa (GOAL-006): sem tick, sem POCs — mas o
        // jogo segue intacto.
        return;
    }

    ls::diag::milestone("plugin inicializado; rodadas de POC a cada "
                        "intervalo configurado (LsConfig.h)");
    ls::diag::flush();
}
