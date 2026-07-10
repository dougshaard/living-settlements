// Living Settlements — core/Diagnostics.h
// POC-010: diagnóstico mínimo. Grava em arquivo próprio
// (living_settlements.log) e espelha erros no debug log do RE_Kenshi
// (Debug.h: DebugLog/ErrorLog são funções livres do KenshiLib).
//
// Threading: chamar SOMENTE da main thread (ADR-014). O logger não tem
// sincronização por desenho — todo o plugin vive no tick da main thread.
#ifndef LS_CORE_DIAGNOSTICS_H
#define LS_CORE_DIAGNOSTICS_H

#include <string>

namespace ls {
namespace diag {

// Abre o arquivo de log (append) e escreve o banner. Chamar uma vez em
// startPlugin. Retorna false se o arquivo não pôde ser aberto (o plugin
// segue funcionando só com o DebugLog do RE_Kenshi).
bool init(const std::string& bannerLine);

// Linha comum: só no arquivo (evita inundar o debug log do RE_Kenshi).
void log(const std::string& line);

// Erro: arquivo + ErrorLog do RE_Kenshi.
void error(const std::string& line);

// Marco importante (instalação de hook, gate, veredito de POC):
// arquivo + DebugLog do RE_Kenshi.
void milestone(const std::string& line);

// Descarrega o buffer para o disco (chamado ao fim de cada rodada de POCs).
void flush();

} // namespace diag
} // namespace ls

#endif // LS_CORE_DIAGNOSTICS_H
