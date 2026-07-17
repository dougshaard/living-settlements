// Living Settlements -- ui/BuildingPanel.h
// -----------------------------------------------------------------
// PAINEL POR PREDIO (teste do dono, 17/07): clicar num edificio abre
// uma janela FECHAVEL com o nome e a posicao dele. E o alicerce da
// visao declarativa -- cada predio podera ter sua config (quartel,
// hospital, deposito tipado...). Aqui so provamos "da p/ criar GUI
// por predio": nome + coordenadas (mundo e relativo ao centro da base).
//
// Deteccao SEM hook novo: PlayerInterface::selectedObject (0x248) e o
// ultimo objeto clicado; hand::getBuilding() resolve p/ Building* (0 se
// o clique foi char/item/nada). Poll por FRAME no tick (main thread),
// antes do gate de pausa -- clicar predio com o jogo pausado tambem
// abre. A janela e criada UMA vez (lazy) e so re-preenchida a cada
// predio novo; o botao de fechar (skin CX) a esconde.
// -----------------------------------------------------------------
#ifndef LS_UI_BUILDINGPANEL_H
#define LS_UI_BUILDINGPANEL_H

class GameWorld;

namespace ls {
namespace ui {

// Chamar a cada frame do mainLoop (main thread). Le a selecao; se um
// predio NOVO foi clicado, abre/atualiza a janela. Nao-op barato quando
// a selecao nao mudou ou nao e predio. Nunca lanca (guardado no chamador).
void pollBuildingSelection(GameWorld* world);

} // namespace ui
} // namespace ls

#endif // LS_UI_BUILDINGPANEL_H
