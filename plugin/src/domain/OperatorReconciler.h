// Living Settlements -- domain/OperatorReconciler.h
// -----------------------------------------------------------------
// Formula CANONICA de reconciliacao de reserva de slot de operador.
// Fonte: docs/design/nucleo-de-trabalho.md secao 5.1 (correcao
// obrigatoria must_fix #2). Existe UMA formula, aqui, usada em todo
// o codigo -- impede dupla-atribuicao contra trabalhadores NATIVOS.
//
// Problema: available() = max(0, fisico - reservado), e "reservado"
// conta SO leases do mod. Se setPhysical recebesse numOperatorsMax cru,
// um slot ocupado por um trabalhador nativo (sem lease do mod) ainda
// apareceria como available > 0 -> o mod despacharia para um slot que
// o nativo ja opera. A reserva existe para impedir exatamente isso.
//
// Formula (por estacao):
//   ocupantesNativos = |currentOperators| - (operadores nossos entre eles)
//   fisicoEfetivo    = numOperatorsMax - ocupantesNativos     (saturado em 0)
//   setPhysical("cap:<building-uid>", fisicoEfetivo)
//
// Algebra: com M=operatorsMax, O=|currentOperators|, Ours=nossos entre
// os ocupantes, Native=O-Ours, R=reservado (soma dos nossos leases):
//   available = max(0, (M-Native) - R) = max(0, (M-O) - Pending)
// onde Pending = R - Ours = workers que despachamos e ainda nao chegaram.
// Ou seja: vagas fisicas livres menos as ja prometidas a workers em
// transito -- sem colidir com o nativo, sem contar duas vezes.
//
// Puro (sem KenshiLib). C++03. ASCII-only.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_OPERATORRECONCILER_H
#define LS_DOMAIN_OPERATORRECONCILER_H

#include "domain/WorkModel.h"
#include <string>

namespace ls {
namespace domain {

// Chave canonica de capacidade de operador de uma estacao (secao 3.1):
// "cap:<building-uid>". Sem sufixo de slot -- operadores sao unidades
// fungiveis reservadas por quantidade, nao por indice de slot.
std::string operatorCapKey(const StationId& stationId);

// Fisico efetivo saturado em [0, +inf). oursAmongCurrent = quantos dos
// currentOperators casam com leases ativos do mod nesta estacao.
int effectivePhysicalSlots(int operatorsMax, int currentOperatorCount,
                           int oursAmongCurrent);

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_OPERATORRECONCILER_H
