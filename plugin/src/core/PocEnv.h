// Living Settlements -- core/PocEnv.h
// -----------------------------------------------------------------
// Flags de POC por VARIAVEL DE AMBIENTE (Fase A). Diferente dos toggles
// de compilacao do LsConfig.h: a MESMA DLL commitada fica com tudo OFF
// (env ausente = OFF, fail-closed) e o experimento liga sem rebuild,
// setando a env ANTES de lancar o jogo.
//
//   LS_ENABLE_POC_MED=1   liga a POC-MED-1 (papel medico)
//   LS_ENABLE_POC_TUR=1   liga a POC-TUR-1 (guarda de torre)
//   LS_POC_WORKER=<nome>  nome EXATO do personagem alvo (obrigatorio p/ emitir)
//   LS_POC_REVERT=1       modo REVERSAO: remove o cargo criado, nao emite
//   LS_POC_TURRET=<uid>   (opcional) uid exato da torre alvo; ausente = a
//                         torre mais proxima do worker
//
// Leitura UMA vez (lazy, main thread) e cacheada -- env de processo nao
// muda em voo. logPocEnv() registra o que foi visto (1a coisa a conferir
// quando "a flag nao pegou").
// -----------------------------------------------------------------
#ifndef LS_CORE_POCENV_H
#define LS_CORE_POCENV_H

#include <string>

namespace ls {
namespace core {

struct PocEnvState {
    bool        medEnabled;   // LS_ENABLE_POC_MED == "1"
    bool        turEnabled;   // LS_ENABLE_POC_TUR == "1"
    bool        revert;       // LS_POC_REVERT == "1"
    std::string worker;       // LS_POC_WORKER (nome exato; "" = ausente)
    std::string turretUid;    // LS_POC_TURRET (uid exato; "" = mais proxima)
    PocEnvState() : medEnabled(false), turEnabled(false), revert(false) {}
};

// Snapshot cacheado do ambiente (primeira chamada le; depois so retorna).
const PocEnvState& pocEnv();

// Loga (diag) o estado visto -- chamar uma vez no startPlugin.
void logPocEnv();

} // namespace core
} // namespace ls

#endif // LS_CORE_POCENV_H
