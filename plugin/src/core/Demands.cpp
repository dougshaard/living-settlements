// Living Settlements -- core/Demands.cpp
// Quadro de demandas (espelho p/ a GUI). ASCII-only. Main thread only.
#include "core/Demands.h"

namespace ls {
namespace core {

namespace {
std::vector<std::string> g_lines;
} // namespace

void demandsSet(const std::vector<std::string>& lines) {
    g_lines = lines;
}

const std::vector<std::string>& demandsGet() {
    return g_lines;
}

} // namespace core
} // namespace ls
