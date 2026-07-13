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

// -----------------------------------------------------------------
// Marco 0 -- POC do nucleo de coordenacao de trabalho (leitura + sombra).
// Retrato do trabalho + dominio em SOMBRA (reserva 5.1, debounce, ledger,
// assignment). ZERO escrita. Fonte: docs/design/nucleo-de-trabalho.md.
// -----------------------------------------------------------------
static const bool  LS_ENABLE_POC020 = true;

// GATE MESTRE DE ESCRITA. Fica false ate H1/H2 serem provados in-game
// (Marco 2). Com false, o LifecycleGate nunca retorna OBSERVE_AND_ACT e
// o OrderEmitter nunca escreve -- postura SOMBRA (invariante 15).
static const bool  LS_M0_WRITES_ENABLED = false;

static const float  LS_M0_RADIUS = 128.0f;   // piso; TownBase::getRadius() amplia
static const int    LS_M0_MAX_RESULTS = 512; // teto de edificios por varredura
static const int    LS_M0_DEBOUNCE_N = 3;    // leituras consecutivas p/ acionavel
static const double LS_M0_LEASE_TTL_HOURS = 24.0; // TTL do lease (horas-de-jogo)
static const double LS_M0_GRACE_HOURS = 2.0;      // graca antes de FAILED
static const int    LS_M0_MAX_PER_ROUND = 4;      // teto de propostas por rodada
static const int    LS_M0_DETAIL_BUDGET = 12;     // linhas de detalhe no log

// Proximidade: pool so inclui workers a ate (raio-da-base * fator) da base
// -- nao arrancar um esquadrao destacado do outro lado do mapa (achado do
// 1o run: idosos destacados a ~73km). Fallback fixo se raio vier 0.
static const double LS_M0_BASE_DIST_FACTOR = 1.5;
static const double LS_M0_MAX_BASE_DIST = 300.0;  // fallback (metros)

// Rastreio (DEBUG) de um personagem por nome: loga cargo, skill, distancia e
// por que ele entra (ou nao) no pool despachavel. "" DESLIGA (padrao). Para
// depurar localmente, ponha um nome aqui -- SEM commitar essa linha.
static const char* const LS_M0_TRACK_WORKER = "";

// Marco 2 -- probe de ciclo de vida (prova H1/H2). Edge-loga os sinais
// isLoadingFromASaveGame/gameResetting/isPaused/allThreadQueuesAreClear
// ao longo de save/quit/new-game. So leitura.
static const bool   LS_ENABLE_POC021 = true;
// allThreadQueuesAreClear alterna MUITO rapido (dezenas de vezes/s): nao
// edge-logar; resumir a taxa de "filas limpas" a cada N ticks.
static const int    LS_M2_SUMMARY_TICKS = 1800;

// -----------------------------------------------------------------
// Parte 5 -- camada de recursos/estoque (P5-0, SOMBRA). So os parametros da
// banda morta + peso + debounce; dwell/breaker/watchdog entram com a escrita
// (P5-3), pois sao malha-fechada. Fonte: docs/design/parte5-recursos-estoque.md.
// -----------------------------------------------------------------
static const bool   LS_ENABLE_STOCK = true;              // StockPolicy em sombra
static const int    LS_RES_CRIT_SINKS_FOR_HUNGRY = 1;    // MIN sinks p/ HUNGRY
static const double LS_RES_FILL_MIN = 0.15;              // banda FALLBACK (sem need-list)
static const double LS_RES_FILL_TARGET = 0.85;
static const double LS_RES_BAND_MIN_GAP = 0.40;          // clamp: ALVO-MIN >= isto
static const int    LS_RES_DEMAND_CONSISTENT_TICKS = 3;  // debounce do tier
static const double LS_RES_STAFF_MAX_LOAD_RATIO = 0.50;  // nao despachar pesado (inv.17)
static const double LS_RES_HANDS_EMPTY_EPS = 0.1;        // "maos vazias" (inv.17)
static const int    LS_RES_MAX_WOULD_STAFF_PER_ROUND = 8;// rate-limit do log
static const int    LS_RES_DETAIL_BUDGET = 14;           // linhas de recurso no log

// DEBUG: marcadores de fase (flushados) dentro do snapshot p/ localizar crash.
// DESLIGADO: a nova iteracao de estoque foi validada in-game sem hang (33 rodadas
// A=A2=B). Religar so se voltar a mexer no loop do snapshot.
static const bool   LS_TRACE_SNAP_PHASES = false;
static const bool   LS_RES_READ_INPUTS = true;   // getConsumtionItems amounts
static const bool   LS_F8_RESOLVE = true;        // permajob subject -> getBuilding

// Estoque-da-base (sec.4.4): le o inventario REAL das storages (getNumItems por
// item produzido) -> sinal da SUBIDA-OBSERVADA. NOVA iteracao nativa (mesma
// familia do hang de 12:12): CAPADA e bisectavel. Manter LS_TRACE_SNAP_PHASES
// LIGADO ate esta superficie ser validada sem hang in-game.
static const bool   LS_RES_READ_BASE_STOCK = true;
static const int    LS_RES_MAX_STORAGE_SCAN = 256; // teto de storages varridas

// -----------------------------------------------------------------
// POC-H11 (P5-3) -- PRIMEIRA ESCRITA controlada do projeto. Prova a hipotese H11:
// addJob(getDefaultTask(), mina) cria um permajob DURAVEL (cargo que persiste,
// como o "Operando maquina: Recurso de Ferro" do Douglas)? Escopo MINUSCULO e
// reversivel: escolhe UM worker ocioso-LIVRE (0 permajobs) + UMA mina operavel,
// emite addJob UMA vez atras da cerca provada (writeGateOpen = modo + save-fence +
// filas limpas), observa N rodadas, e REVERTE com removePermajob so de MAOS VAZIAS
// (inv.17/18). Single-shot: apos o veredito nao age mais (ate recarregar a DLL).
//
// Switch PROPRIO: LS_M0_WRITES_ENABLED continua FALSE (o coordenador geral/Poc020
// segue 100% SOMBRA). So este experimento escreve, e so quando LS_ENABLE_H11=true.
// !!! DESLIGAR (false) ANTES DE COMMITAR -- e experimento, nao comportamento. !!!
// REFUTADO in-game (2026-07-11): addJob(OPERATE_MACHINERY, mina) NAO cria cargo
// duravel -- OPERATE_MACHINERY e uma ACAO transiente -> cai na lista `jobs`, nao
// `permajobs`, e o GOAP passa por cima. A API (addJob) estava certa; o TaskType,
// errado. Desligado. Antes de tentar de novo, o AIPROBE (read-only) revela o
// TaskType REAL que o painel de jobs do Kenshi usa p/ minerar.
static const bool  LS_ENABLE_H11 = false; // primitiva provada; desligado. reverter ja esta false p/ commit
// EXPERIMENTO "cerebro organiza a base" (Poc025): limpa TODOS os cargos de TODOS +
// staffa os ociosos na producao com alocacao inteligente (multi-slot + ranking por
// necessidade/concentracao + skill-aware). Provado in-game (achados sec.26). OFF por
// padrao (experimento de escrita; a Etapa 0 e sombra).
static const bool  LS_ENABLE_ORGANIZER = false;
// EXPERIMENTO JOBHOOK: hooka as funcoes da UI de jobs p/ capturar a chamada
// exata que o jogador faz. READ-ONLY (loga e chama o original). DESLIGAR antes
// de commit (Etapa 0 nao instala hooks alem do tick).
static const bool  LS_ENABLE_JOBHOOK = false; // captura ja feita; nem hooka Character::addJob
static const char* const LS_H11_WORKER = ""; // nome EXATO p/ mirar PER-CHAR; "" = selecionado (AIPROBE tb usa). SEM commitar um nome.
static const int   LS_H11_OBSERVE_ROUNDS = 4;  // rodadas observando antes de reverter
static const int   LS_H11_GIVEUP_ROUNDS = 30;  // desiste de reverter (maos cheias) apos isto

// AIPROBE (read-only): despeja o sistema de tarefas da IA (getAI()->getTaskSystem())
// do personagem SELECIONADO (ou LS_H11_WORKER): as listas `jobs` e `permajobs` com
// key()/subject/isPermaJob e o getPermajobCount. Objetivo: o jogador atribui um job
// de mina pelo painel NATIVO do Kenshi e a gente VE em qual lista cai e QUAL TaskType
// e (resolve o misterio do H11). ZERO escrita.
static const bool  LS_ENABLE_AIPROBE = false;

// Arquivo de log (POC-010). Caminho relativo ao diretório de trabalho
// do processo do jogo (a pasta de instalação do Kenshi).
static const char* const LS_LOG_FILE = "living_settlements.log";

} // namespace ls

#endif // LS_CORE_LSCONFIG_H
