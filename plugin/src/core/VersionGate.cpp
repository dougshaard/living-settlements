// Living Settlements — core/VersionGate.cpp
#include "core/VersionGate.h"
#include "core/Diagnostics.h"

#include <string>

#include <kenshi/Kenshi.h> // KenshiLib::GetKenshiVersion / BinaryVersion

namespace ls {
namespace core {

namespace {

// Allowlist explicita de versoes TESTADAS (achado da revisao: ADR-011
// exige fail-closed no eixo VERSAO, nao so no eixo plataforma — nada
// garante que plataforma reconhecida implique RVAs suportados).
// Fonte: FACT-008 — RE_Kenshi 0.3.4 declara suporte a 1.0.65 e 1.0.68.
// Ao validar uma nova versao em jogo, adicionar aqui conscientemente.
const char* const LS_TESTED_VERSIONS[] = { "1.0.65", "1.0.68" };
const int LS_TESTED_VERSIONS_COUNT =
    static_cast<int>(sizeof(LS_TESTED_VERSIONS) / sizeof(LS_TESTED_VERSIONS[0]));

bool isTestedVersion(const std::string& v) {
    for (int i = 0; i < LS_TESTED_VERSIONS_COUNT; ++i) {
        if (v == LS_TESTED_VERSIONS[i])
            return true;
    }
    return false;
}

} // namespace

bool versionGateAllowsHooks() {
    KenshiLib::BinaryVersion version = KenshiLib::GetKenshiVersion();

    diag::milestone("version gate: Kenshi " + version.ToString());

    if (version.GetPlatform() == KenshiLib::BinaryVersion::UNKNOWN) {
        // ADR-011: nenhum hook em plataforma desconhecida.
        diag::error("version gate REPROVADO: plataforma desconhecida -- "
                    "nenhum hook sera instalado (ADR-011, REQ-CORE-008)");
        return false;
    }

    if (!isTestedVersion(version.GetVersion())) {
        // ADR-011: fail-closed tambem por VERSAO. O RE_Kenshi ja
        // restringe as versoes que aceita (FACT-008), mas o plugin
        // mantem a propria porta fechada — versao nova de jogo pode
        // invalidar RVAs sem mudar a plataforma (RISK-003).
        diag::error("version gate REPROVADO: versao \"" + version.GetVersion()
                    + "\" fora da allowlist testada (1.0.65/1.0.68) -- "
                    "nenhum hook sera instalado (ADR-011, REQ-CORE-008)");
        return false;
    }

    diag::milestone("version gate aprovado: hooks liberados");
    return true;
}

} // namespace core
} // namespace ls
