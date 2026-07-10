// Living Settlements — core/TickHost.h
// ADR-014: ponto de hook ÚNICO do coordenador, na main thread do jogo
// (GameWorld::mainLoop_GPUSensitiveStuff). Todo acesso a estado do jogo
// (leitura e, nas etapas futuras, emissão de ordens) acontece aqui.
// Nunca ler/mutar estado de worker thread (RISK-011).
//
// ADR-013: a observação é por polling — este tick é o "relógio" do
// coordenador; o WorldSnapshot e o diffing serão pendurados aqui.
#ifndef LS_CORE_TICKHOST_H
#define LS_CORE_TICKHOST_H

namespace ls {
namespace core {

// Instala o hook do tick via KenshiLib::AddHook na variante _NV_ do
// mainLoop (GetRealAddress não funciona com virtuais — core/Functions.h).
// Retorna false se o hook falhou (o plugin degrada para no-op logado,
// GOAL-006).
bool installTickHook();

} // namespace core
} // namespace ls

#endif // LS_CORE_TICKHOST_H
