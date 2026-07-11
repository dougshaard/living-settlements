// Living Settlements -- domain/Debouncer.cpp
// Puro, C++03, ASCII-only.
#include "domain/Debouncer.h"

namespace ls {
namespace domain {

Debouncer::Debouncer(int threshold) : threshold_(threshold) {
    if (threshold_ < 1) {
        threshold_ = 1;
    }
}

void Debouncer::observe(const std::vector<std::string>& activeKeys) {
    // Reconstroi a tabela so com as chaves presentes AGORA: quem sumiu
    // e zerado (a condicao precisa ser continua). Quem persiste soma +1,
    // saturado no limiar.
    std::map<std::string, int> next;
    for (std::size_t i = 0; i < activeKeys.size(); ++i) {
        const std::string& k = activeKeys[i];
        int prev = 0;
        std::map<std::string, int>::const_iterator it = counts_.find(k);
        if (it != counts_.end()) {
            prev = it->second;
        }
        int c = prev + 1;
        if (c > threshold_) {
            c = threshold_;
        }
        next[k] = c;
    }
    counts_.swap(next);
}

bool Debouncer::isStable(const std::string& key) const {
    return count(key) >= threshold_;
}

int Debouncer::count(const std::string& key) const {
    std::map<std::string, int>::const_iterator it = counts_.find(key);
    return (it == counts_.end()) ? 0 : it->second;
}

std::size_t Debouncer::trackedCount() const {
    return counts_.size();
}

void Debouncer::clear() {
    counts_.clear();
}

} // namespace domain
} // namespace ls
