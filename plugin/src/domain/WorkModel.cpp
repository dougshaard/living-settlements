// Living Settlements -- domain/WorkModel.cpp
// Implementacao dos helpers do modelo de dados puro (WorkModel.h).
// C++03, sem dependencia de KenshiLib. ASCII-only.
#include "domain/WorkModel.h"

#include <sstream>

namespace ls {
namespace domain {

int WorkerView::skillFor(int statId) const {
    for (size_t i = 0; i < skills.size(); ++i) {
        if (skills[i].statId == statId) {
            return skills[i].level;
        }
    }
    return -1; // desconhecido no snapshot
}

bool WorkerView::servesMachine(const StationId& station) const {
    if (station.empty()) {
        return false;
    }
    for (size_t i = 0; i < permajobs.size(); ++i) {
        if (permajobs[i].roleMachineId == station) {
            return true;
        }
    }
    return false;
}

// Chave canonica deterministica. Uma barra separa os campos; como
// StationId (uid) e verb sao livres de '|', a concatenacao e injetiva
// o bastante para dedup (secao 3.1). Nao e hash numerico de proposito:
// string canonica e deterministica e trivial de auditar no log.
TaskKey makeTaskKey(const StationId& station, int verb, const std::string& resource) {
    std::ostringstream os;
    os << "v" << verb << "|s" << station << "|r" << resource;
    return os.str();
}

double distanceSquared(double ax, double ay, double az,
                       double bx, double by, double bz) {
    double dx = ax - bx;
    double dy = ay - by;
    double dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace domain
} // namespace ls
