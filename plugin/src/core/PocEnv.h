// Living Settlements -- core/PocEnv.h
// -----------------------------------------------------------------
// Toggles de POC da Fase A. Diferente dos toggles de compilacao do
// LsConfig.h: a MESMA DLL commitada fica com tudo OFF (fonte ausente =
// OFF, fail-closed) e o experimento liga sem rebuild. SO ferramenta de
// desenvolvimento -- o produto e configurado in-game/por sidecar.
//
// FONTE PRIMARIA: arquivo `mods\LivingSettlements\poc.txt` (relativo ao
// diretorio de trabalho do jogo, o mesmo do living_settlements.log).
// Linhas KEY=VALUE, `#` comenta, ausente = tudo OFF. Edita com qualquer
// editor de texto e lanca o jogo NORMAL (Steam) -- sem terminal.
// Variavel de ambiente com o MESMO nome, se presente e nao-vazia,
// SOBREPOE a do arquivo (automacao/execucao via shell).
//
//   LS_ENABLE_POC_MED=1   liga a POC-MED-1 (papel medico)
//   LS_ENABLE_POC_TUR=1   liga a POC-TUR-1 (guarda de torre)
//   LS_POC_WORKER=<nome>  nome EXATO do personagem alvo (obrigatorio p/ emitir)
//   LS_POC_REVERT=1       modo REVERSAO: remove o cargo criado, nao emite
//   LS_POC_TURRET=<uid>   (opcional) uid exato da torre alvo; ausente = a
//                         torre mais proxima do worker
//
// Leitura UMA vez (lazy, main thread) e cacheada -- editar o poc.txt com
// o jogo aberto NAO tem efeito (reiniciar o jogo). logPocEnv() registra
// o que foi visto (1a coisa a conferir quando "a flag nao pegou").
// -----------------------------------------------------------------
#ifndef LS_CORE_POCENV_H
#define LS_CORE_POCENV_H

#include <string>

namespace ls {
namespace core {

struct PocEnvState {
    bool        medEnabled;   // LS_ENABLE_POC_MED == "1"
    bool        turEnabled;   // LS_ENABLE_POC_TUR == "1"
    bool        garrison;     // LS_GARRISON == "1": guarnicao automatica de torres
    bool        orchestrator; // LS_ORCHESTRATOR == "1": organizador CONTINUO de
                              // producao/fazendas (Poc025 sem a fase de limpeza)
    bool        haul;         // LS_HAUL == "1": carregador dirigido (Poc029),
                              // laco automatico de transporte por demanda
    bool        haulOnce;     // gatilho do painel (dir.14): 1 ciclo de haul
                              // mesmo com o laco desligado; a POC consome
    bool        revert;       // LS_POC_REVERT == "1"
    std::string worker;       // LS_POC_WORKER (nome exato; "" = ausente)
    std::string medWorker;    // LS_POC_MED_WORKER; vazio = cai em worker.
                              // Permite MED e TUR juntas com alvos separados.
    std::string turretUid;    // LS_POC_TURRET (uid exato; "" = mais proxima)
    // Ajuste manual da guarnicao (decisao 9: automatico + ajuste; diretriz 11:
    // a declaracao e do JOGADOR). CSV de uids (do log). ONLY presente = so
    // essas torres sao postos; senao todas, MENOS as de EXCLUDE.
    std::string garrisonOnly;    // LS_GARRISON_ONLY=uid1,uid2
    std::string garrisonExclude; // LS_GARRISON_EXCLUDE=uid1,uid2
    PocEnvState() : medEnabled(false), turEnabled(false), garrison(false),
                    orchestrator(false), haul(false), haulOnce(false),
                    revert(false) {}
};

// Snapshot cacheado do ambiente (primeira chamada le; depois so retorna).
const PocEnvState& pocEnv();

// Estado MUTAVEL em runtime (GUI/painel de controle; main thread only).
// As POCs releem pocEnv() a cada rodada -> mudar aqui e controle AO VIVO.
// poc.txt/env continuam sendo apenas o DEFAULT de boot.
PocEnvState& pocEnvMutable();

// Loga (diag) o estado visto -- chamar uma vez no startPlugin.
void logPocEnv();

} // namespace core
} // namespace ls

#endif // LS_CORE_POCENV_H
