// Living Settlements — pocs/Poc002_EnumerarBase.h
// POC-002 (obrigatório): "Descobrir armazenamentos, máquinas, fazendas,
// construções, leitos, portões e torres." Etapa 0: enumeração espacial
// (GameWorld::getObjectsWithinSphere) + assentamento nativo (TownBase,
// FACT-012). Classificação fina por subtipo entra com POC-003.
//
// Decisão de segurança: NÃO usamos TownBase::findAllBuildingsWithFunction
// nesta POC — o ownership do lektor<Building*>* retornado é desconhecido
// (ver unknowns do recon; doc §0.2: não presumir capacidades).
#ifndef LS_POCS_POC002_H
#define LS_POCS_POC002_H

class GameWorld;

namespace ls {
namespace pocs {

// A partir do primeiro personagem válido do jogador: registra o
// assentamento corrente (nome, tipo, estado de alarme — FACT-012) e
// enumera edifícios num raio configurável, com contagem por função
// (BuildingFunction) e marcação de danificados.
void poc002Run(GameWorld* world);

} // namespace pocs
} // namespace ls

#endif // LS_POCS_POC002_H
