// Living Settlements -- domain/Debouncer.h
// -----------------------------------------------------------------
// ObservationDebouncer (secao 3.3 / 5 passo 5 / 7.2). Uma condicao so
// vira "acionavel" apos N leituras CONSECUTIVAS consistentes. Absorve:
//   - getCurrentAction ciclico/ruidoso [R] (idle intermitente);
//   - flicker de productionState STARVED/FULL [R];
//   - reordenacao do roster (chaves sao strings estaveis, nao indices).
//
// Uso: a cada tick, observe() recebe o conjunto de chaves ATIVAS agora
// (ex.: "unmanned:<stationId>", "idle:<workerId>"). Chaves ausentes num
// tick sao ZERADAS (a condicao precisa ser continua). isStable() diz se
// a contagem atingiu o limiar.
//
// Puro (sem KenshiLib). C++03. ASCII-only.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_DEBOUNCER_H
#define LS_DOMAIN_DEBOUNCER_H

#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace ls {
namespace domain {

class Debouncer {
public:
    // threshold = numero de leituras consecutivas para ficar estavel.
    // threshold <= 1 => estavel na primeira observacao.
    explicit Debouncer(int threshold);

    // Chamar UMA vez por tick com todas as chaves ativas agora.
    void observe(const std::vector<std::string>& activeKeys);

    bool isStable(const std::string& key) const;  // count >= threshold
    int  count(const std::string& key) const;      // 0 se ausente
    std::size_t trackedCount() const;
    void clear();

private:
    int threshold_;
    std::map<std::string, int> counts_;
};

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_DEBOUNCER_H
