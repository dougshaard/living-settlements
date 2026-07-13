// Living Settlements -- pocs/Poc024_JobHook.h
// Hook READ-ONLY (loga e chama o original) das funcoes da UI de jobs, p/
// capturar a CHAMADA EXATA que o jogador faz ao atribuir uma mina (task,
// subject, flags, pos). Depois replicamos byte-a-byte -- fim do palpite.
#ifndef LS_POCS_POC024_JOBHOOK_H
#define LS_POCS_POC024_JOBHOOK_H

namespace ls {
namespace pocs {

// Instala os detours (addJobSelectedCharacters + newPlayerTaskSelectedCharacters).
// Chamado uma vez em startPlugin, atras de LS_ENABLE_JOBHOOK. Retorna true se
// todos os hooks foram instalados.
bool installJobHooks();

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC024_JOBHOOK_H
