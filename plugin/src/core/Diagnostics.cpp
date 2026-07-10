// Living Settlements — core/Diagnostics.cpp
#include "core/Diagnostics.h"
#include "core/LsConfig.h"

#include <Debug.h> // DebugLog/ErrorLog (KenshiLib)

#include <fstream>
#include <ctime>

namespace ls {
namespace diag {

namespace {

// Estado do logger. Acesso exclusivo pela main thread (ADR-014); a
// inicialização acontece em startPlugin, antes de qualquer tick.
std::ofstream g_file;
bool g_fileOk = false;

std::string timestamp() {
    // Hora local curta [HH:MM:SS]. time/localtime bastam para a Etapa 0;
    // o relógio de jogo entra quando o snapshot existir (ADR-013).
    std::time_t t = std::time(0);
    std::tm* lt = std::localtime(&t);
    char buf[16];
    if (lt != 0) {
        std::strftime(buf, sizeof(buf), "%H:%M:%S", lt);
        return std::string("[") + buf + "] ";
    }
    return std::string("[--:--:--] ");
}

void writeFile(const std::string& line) {
    if (g_fileOk) {
        // Flush POR LINHA (robustez): um access
        // violation dentro de uma POC mata o processo sem esvaziar o
        // buffer de user-space — e o log e a UNICA evidencia do gate
        // A/B/C (POC-010). O custo de I/O e irrelevante na cadencia da
        // Etapa 0 (rodadas a cada ~10s).
        g_file << timestamp() << line << std::endl;
    }
}

} // namespace

bool init(const std::string& bannerLine) {
    g_file.open(LS_LOG_FILE, std::ios::out | std::ios::app);
    g_fileOk = g_file.is_open();
    if (g_fileOk) {
        g_file << "\n";
        writeFile("==== " + bannerLine + " ====");
        g_file.flush();
    } else {
        // Sem arquivo: seguimos só com o debug log do RE_Kenshi.
        ErrorLog(std::string(LS_NAME) + ": nao foi possivel abrir " + LS_LOG_FILE);
    }
    DebugLog(std::string(LS_NAME) + ": " + bannerLine);
    return g_fileOk;
}

void log(const std::string& line) {
    writeFile(line);
}

void error(const std::string& line) {
    writeFile(std::string("ERRO: ") + line);
    ErrorLog(std::string(LS_NAME) + ": " + line);
}

void milestone(const std::string& line) {
    writeFile(line);
    DebugLog(std::string(LS_NAME) + ": " + line);
}

void flush() {
    if (g_fileOk)
        g_file.flush();
}

} // namespace diag
} // namespace ls
