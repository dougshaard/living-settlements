// Living Settlements -- core/Demands.h
// -----------------------------------------------------------------
// QUADRO DE DEMANDAS (diretriz 13, saindo do log p/ a GUI): o transporte
// (Poc029) publica aqui, a cada rodada, o retrato acionavel da logistica --
// o que esta EM TRANSPORTE, o que falta PRODUZIR, onde falta ARMAZEM ou
// CARREGADOR. A GUI le apenas este espelho (mesmo padrao do roster em
// core/Porters): nenhuma leitura de mundo no clique.
//
// Main thread only (produtor e consumidor rodam nela; ADR-014).
// -----------------------------------------------------------------
#ifndef LS_CORE_DEMANDS_H
#define LS_CORE_DEMANDS_H

#include <string>
#include <vector>

namespace ls {
namespace core {

// Substitui o quadro inteiro (chamado pelo transporte ao fim da rodada).
void demandsSet(const std::vector<std::string>& lines);

// Quadro corrente (linhas prontas p/ exibir; cap aplicado no produtor).
const std::vector<std::string>& demandsGet();

} // namespace core
} // namespace ls

#endif // LS_CORE_DEMANDS_H
