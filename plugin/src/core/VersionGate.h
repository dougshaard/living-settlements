// Living Settlements — core/VersionGate.h
// ADR-011 (falha fechada em versão desconhecida) + FACT-017 (KenshiLib
// detecta plataforma/versão em runtime via GetKenshiVersion).
// REQ-CORE-008: versões desconhecidas desativam hooks arriscados.
#ifndef LS_CORE_VERSIONGATE_H
#define LS_CORE_VERSIONGATE_H

namespace ls {
namespace core {

// Consulta KenshiLib::GetKenshiVersion(), registra plataforma+versão no
// diagnóstico e decide se os hooks podem ser instalados.
// Fail-closed: plataforma UNKNOWN => false (o plugin vira no-op logado).
bool versionGateAllowsHooks();

} // namespace core
} // namespace ls

#endif // LS_CORE_VERSIONGATE_H
