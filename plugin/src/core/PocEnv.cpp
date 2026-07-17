// Living Settlements -- core/PocEnv.cpp
// Toggles de POC: arquivo poc.txt (fonte primaria, edita e lanca normal)
// + env-var como override (GetEnvironmentVariableA le o bloco VIVO do
// processo, independente da copia do CRT). ASCII-only.
#include "core/PocEnv.h"
#include "core/Diagnostics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fstream>
#include <sstream>

namespace ls {
namespace core {

namespace {

bool        g_loaded = false;
bool        g_fileSeen = false;
PocEnvState g_state;

// Caminho relativo ao diretorio de trabalho do processo do jogo -- o mesmo
// contrato do LS_LOG_FILE (o living_settlements.log aparece na pasta de
// instalacao do Kenshi, mesmo com o exe real vivendo em RE_Kenshi\).
static const char* const POC_FILE = "mods\\LivingSettlements\\poc.txt";
static const int         POC_FILE_MAX_LINES = 64; // cap duro (arquivo lixo)

std::string trim(const std::string& s) {
    std::string::size_type a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) {
        ++a;
    }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) {
        --b;
    }
    return s.substr(a, b - a);
}

// Le o poc.txt (KEY=VALUE; '#' comenta; chaves desconhecidas ignoradas).
// Retorna se o arquivo existia. Ausente/ilegivel = tudo OFF (fail-closed).
bool readPocFile(PocEnvState& st) {
    std::ifstream f(POC_FILE);
    if (!f.is_open()) {
        return false;
    }
    std::string line;
    int guard = 0;
    while (std::getline(f, line) && ++guard <= POC_FILE_MAX_LINES) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        std::string::size_type eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string k = trim(t.substr(0, eq));
        std::string v = trim(t.substr(eq + 1));
        if (k == "LS_ENABLE_POC_MED") {
            st.medEnabled = (v == "1");
        } else if (k == "LS_ENABLE_POC_TUR") {
            st.turEnabled = (v == "1");
        } else if (k == "LS_GARRISON") {
            st.garrison = (v == "1");
        } else if (k == "LS_GARRISON_ONLY") {
            st.garrisonOnly = v;
        } else if (k == "LS_GARRISON_EXCLUDE") {
            st.garrisonExclude = v;
        } else if (k == "LS_ORCHESTRATOR") {
            st.orchestrator = (v == "1");
        } else if (k == "LS_HAUL") {
            st.haul = (v == "1");
        } else if (k == "LS_MEDIC") {
            st.medicRole = (v == "1");
        } else if (k == "LS_POC_REVERT") {
            st.revert = (v == "1");
        } else if (k == "LS_POC_WORKER") {
            st.worker = v;
        } else if (k == "LS_POC_MED_WORKER") {
            st.medWorker = v;
        } else if (k == "LS_POC_TURRET") {
            st.turretUid = v;
        }
    }
    return true;
}

std::string readEnv(const char* name) {
    char buf[256];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        return std::string(); // ausente, vazia ou grande demais
    }
    return std::string(buf, n);
}

} // namespace

const PocEnvState& pocEnv() {
    if (!g_loaded) {
        g_fileSeen = readPocFile(g_state);
        // Env presente e nao-vazia SOBREPOE o arquivo (chave a chave).
        std::string v;
        v = readEnv("LS_ENABLE_POC_MED");
        if (!v.empty()) {
            g_state.medEnabled = (v == "1");
        }
        v = readEnv("LS_ENABLE_POC_TUR");
        if (!v.empty()) {
            g_state.turEnabled = (v == "1");
        }
        v = readEnv("LS_HAUL");
        if (!v.empty()) {
            g_state.haul = (v == "1");
        }
        v = readEnv("LS_MEDIC");
        if (!v.empty()) {
            g_state.medicRole = (v == "1");
        }
        v = readEnv("LS_POC_REVERT");
        if (!v.empty()) {
            g_state.revert = (v == "1");
        }
        v = readEnv("LS_POC_WORKER");
        if (!v.empty()) {
            g_state.worker = v;
        }
        v = readEnv("LS_POC_MED_WORKER");
        if (!v.empty()) {
            g_state.medWorker = v;
        }
        v = readEnv("LS_POC_TURRET");
        if (!v.empty()) {
            g_state.turretUid = v;
        }
        v = readEnv("LS_GARRISON_ONLY");
        if (!v.empty()) {
            g_state.garrisonOnly = v;
        }
        v = readEnv("LS_GARRISON_EXCLUDE");
        if (!v.empty()) {
            g_state.garrisonExclude = v;
        }
        if (g_state.medWorker.empty()) {
            g_state.medWorker = g_state.worker; // fallback: um alvo so
        }
        g_loaded = true;
    }
    return g_state;
}

PocEnvState& pocEnvMutable() {
    pocEnv(); // garante o carregamento do default (arquivo+env) antes de mutar
    return g_state;
}

void logPocEnv() {
    const PocEnvState& e = pocEnv();
    std::ostringstream s;
    s << "FASE-A toggles (poc.txt " << (g_fileSeen ? "LIDO" : "ausente")
      << " + env override): MED=" << (e.medEnabled ? "ON" : "off")
      << " TUR=" << (e.turEnabled ? "ON" : "off")
      << " GUARNICAO=" << (e.garrison ? "ON" : "off")
      << " ORQUESTRADOR=" << (e.orchestrator ? "ON" : "off")
      << " CARREGADOR=" << (e.haul ? "ON" : "off")
      << " MEDICOS=" << (e.medicRole ? "ON" : "off")
      << " REVERT=" << (e.revert ? "ON" : "off")
      << " worker=" << (e.worker.empty() ? std::string("(nenhum)")
                                         : "\"" + e.worker + "\"")
      << " medWorker=" << (e.medWorker.empty() ? std::string("(nenhum)")
                                               : "\"" + e.medWorker + "\"")
      << " torre=" << (e.turretUid.empty() ? std::string("(mais proxima)")
                                           : e.turretUid);
    if (e.medEnabled || e.turEnabled || e.garrison || e.orchestrator || e.haul
        || e.medicRole) {
        diag::milestone(s.str());
    } else {
        diag::log(s.str()); // tudo OFF: linha comum (estado normal da DLL)
    }
}

} // namespace core
} // namespace ls
