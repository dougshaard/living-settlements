// Living Settlements -- domain/OperatorReconciler.cpp
// Implementacao da formula canonica secao 5.1. Puro, C++03, ASCII-only.
#include "domain/OperatorReconciler.h"

namespace ls {
namespace domain {

std::string operatorCapKey(const StationId& stationId) {
    return std::string("cap:") + stationId;
}

int effectivePhysicalSlots(int operatorsMax, int currentOperatorCount,
                           int oursAmongCurrent) {
    // Robustez: nossos leases confirmados nunca deveriam exceder os
    // ocupantes observados; se a leitura vier inconsistente, satura.
    int nativeOccupants = currentOperatorCount - oursAmongCurrent;
    if (nativeOccupants < 0) {
        nativeOccupants = 0;
    }
    int effective = operatorsMax - nativeOccupants;
    if (effective < 0) {
        effective = 0;
    }
    return effective;
}

} // namespace domain
} // namespace ls
