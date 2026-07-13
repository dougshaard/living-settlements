// Living Settlements -- core/PocEnv.cpp
// Leitura de env-vars do processo do jogo (GetEnvironmentVariableA: le o
// bloco VIVO do processo, independente da copia do CRT). ASCII-only.
#include "core/PocEnv.h"
#include "core/Diagnostics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <sstream>

namespace ls {
namespace core {

namespace {

bool        g_loaded = false;
PocEnvState g_state;

std::string readEnv(const char* name) {
    char buf[256];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        return std::string(); // ausente, vazia ou grande demais -> OFF
    }
    return std::string(buf, n);
}

bool flagOn(const char* name) {
    return readEnv(name) == "1"; // so "1" liga; qualquer outra coisa = OFF
}

} // namespace

const PocEnvState& pocEnv() {
    if (!g_loaded) {
        g_state.medEnabled = flagOn("LS_ENABLE_POC_MED");
        g_state.turEnabled = flagOn("LS_ENABLE_POC_TUR");
        g_state.revert     = flagOn("LS_POC_REVERT");
        g_state.worker     = readEnv("LS_POC_WORKER");
        g_state.turretUid  = readEnv("LS_POC_TURRET");
        g_loaded = true;
    }
    return g_state;
}

void logPocEnv() {
    const PocEnvState& e = pocEnv();
    std::ostringstream s;
    s << "FASE-A env: MED=" << (e.medEnabled ? "ON" : "off")
      << " TUR=" << (e.turEnabled ? "ON" : "off")
      << " REVERT=" << (e.revert ? "ON" : "off")
      << " worker=" << (e.worker.empty() ? std::string("(nenhum)")
                                         : "\"" + e.worker + "\"")
      << " torre=" << (e.turretUid.empty() ? std::string("(mais proxima)")
                                           : e.turretUid);
    if (e.medEnabled || e.turEnabled) {
        diag::milestone(s.str());
    } else {
        diag::log(s.str()); // tudo OFF: linha comum (estado normal da DLL)
    }
}

} // namespace core
} // namespace ls
