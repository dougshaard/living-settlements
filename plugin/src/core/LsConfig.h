// Living Settlements — core/LsConfig.h
// Configuração da Etapa 0. Constantes de compilação: simples e sem I/O.
// (PRINC-002 pede dados configuráveis; para a prova técnica, toggles de
// compilação bastam — um arquivo de config entra na Etapa 1.)
#ifndef LS_CORE_LSCONFIG_H
#define LS_CORE_LSCONFIG_H

namespace ls {

// Identidade
static const char* const LS_NAME = "LivingSettlements";
static const char* const LS_VERSION = "0.1.0-etapa0";

// Cadência das POCs (segundos de tempo real acumulado no tick).
// Mantida alta de propósito: a Etapa 0 observa, não coordena.
static const float LS_POC_INTERVAL_SECONDS = 10.0f;

// POC-002: raio de varredura ao redor do personagem (metros) e teto de
// resultados. Valores pequenos de propósito — orçamento por ciclo
// (REQ-CORE-002/RISK-004).
static const float LS_POC002_RADIUS = 64.0f;
static const int   LS_POC002_MAX_RESULTS = 256;

// Toggles das provas técnicas
static const bool LS_ENABLE_POC001 = true;  // enumerar personagens do jogador
static const bool LS_ENABLE_POC002 = true;  // enumerar entidades da base
static const bool LS_ENABLE_POC003 = true;  // detectar ameaca / identificar atacante

// POC-003: raio de varredura de personagens ao redor da base e teto de
// resultados. A identificacao do atacante (getAllAttackers) INDEPENDE do
// raio; a varredura so complementa mostrando hostis proximos.
static const float LS_POC003_RADIUS = 200.0f;
static const int   LS_POC003_MAX_CHARS = 256;
static const int   LS_POC003_DETAIL_BUDGET = 20;

// POC-011 (write-path): DESLIGADA por padrão — ligar somente depois de
// POC-001/002 passarem no primeiro run em jogo (leitura antes de escrita).
static const bool LS_ENABLE_POC011 = false;
static const int   LS_POC011_GRACE_ROUNDS = 2;   // rodadas de leitura antes de emitir
static const int   LS_POC011_OBSERVE_ROUNDS = 6; // janela de observação pós-emissão
static const float LS_POC011_OFFSET_METERS = 4.0f; // deslocamento pedido

// Arquivo de log (POC-010). Caminho relativo ao diretório de trabalho
// do processo do jogo (a pasta de instalação do Kenshi).
static const char* const LS_LOG_FILE = "living_settlements.log";

} // namespace ls

#endif // LS_CORE_LSCONFIG_H
