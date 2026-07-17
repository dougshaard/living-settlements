// Living Settlements -- ui/BuildingPanel.cpp
// Painel por predio (teste). ASCII-only. So simbolos verificados:
//   PlayerInterface::selectedObject (hand, 0x248)      PlayerInterface.h:242
//   hand::getBuilding / isValid / toString             hand.h:52/64/38
//   Building::getName / getInstanceID / getPosition / getTown  (padrao das POCs)
//   MyGUI Window/Button createWidgetReal + eventWindowButtonPressed
// Widgets so nos skins PROVADOS (Kenshi_WindowCX/Kenshi_Button1): botoes
// servem de LINHAS DE TEXTO (renderizam legenda; nenhum StaticText a adivinhar).
#include "ui/BuildingPanel.h"
#include "core/Diagnostics.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Town.h>
#include <kenshi/InstanceID.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>

#include <string>
#include <sstream>

namespace ls {
namespace ui {

namespace {

static const int PB_ROWS = 4;

MyGUI::Window* g_win = 0;
MyGUI::Button* g_row[PB_ROWS] = { 0 };
bool           g_built = false;
std::string    g_lastKey;   // predio tratado por ultimo (mostrado OU fechado)
std::string    g_closedKey; // predio que o jogador FECHOU (fica oculto ate ele
                            // selecionar outro) -- conserta o "impossivel fechar"

void onWindowButton(MyGUI::Window* sender, const std::string& button) {
    // Fechar por QUALQUER botao da barra (o skin CX so tem o X). NAO limpar
    // g_lastKey: o predio segue selecionado, e limpar faria o poll reabrir no
    // frame seguinte (bug "impossivel fechar"). Marcar como fechado e sair.
    diag::log(std::string("PAINEL-PREDIO: botao da janela = '") + button + "'");
    if (sender != 0) {
        sender->setVisible(false);
        g_closedKey = g_lastKey;
    }
}

// Cria a janela UMA vez (lazy; a UI do jogo tem que estar de pe). Retorna
// se esta pronta.
bool ensureBuilt() {
    if (g_built) {
        return g_win != 0;
    }
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) {
        return false; // ainda sem GUI (title screen nao construiu)
    }
    g_win = gui->createWidgetReal<MyGUI::Window>(
        "Kenshi_WindowCX", 0.40f, 0.34f, 0.22f, 0.24f,
        MyGUI::Align::Default, "Window", "LSBuildingPanel");
    g_win->setCaption("Predio");
    g_win->eventWindowButtonPressed += MyGUI::newDelegate(onWindowButton);
    MyGUI::Widget* c = g_win->getClientWidget();
    for (int i = 0; i < PB_ROWS; ++i) {
        g_row[i] = c->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1", 0.04f, 0.03f + 0.235f * i, 0.92f, 0.20f,
            MyGUI::Align::Default, "");
        g_row[i]->setCaption("");
    }
    g_win->setVisible(false);
    g_built = true;
    diag::milestone("PAINEL-PREDIO: janela criada (clique um edificio p/ abrir; "
                    "fechavel no X).");
    return true;
}

void showFor(Building* b) {
    if (!ensureBuilt() || b == 0) {
        return;
    }
    std::string name = b->getName();
    std::string uid;
    {
        InstanceID* iid = b->getInstanceID();
        if (iid != 0) {
            uid = iid->uid;
        }
    }
    Ogre::Vector3 p = b->getPosition();

    // Relativo ao centro da base (se o predio pertence a uma town).
    bool haveRel = false;
    Ogre::Vector3 rel(0, 0, 0);
    {
        TownBase* town = b->getTown();
        if (town != 0) {
            Ogre::Vector3 cen = town->getPosition();
            rel = Ogre::Vector3(p.x - cen.x, p.y - cen.y, p.z - cen.z);
            haveRel = true;
        }
    }

    g_win->setCaption(name.empty() ? std::string("Predio") : name);
    {
        std::ostringstream s; s << "Nome: " << name;
        g_row[0]->setCaption(s.str());
    }
    {
        std::ostringstream s; s << "UID: " << (uid.empty() ? "(em obra?)" : uid);
        g_row[1]->setCaption(s.str());
    }
    {
        std::ostringstream s;
        s << "Mundo x/y/z: " << static_cast<int>(p.x) << " / "
          << static_cast<int>(p.y) << " / " << static_cast<int>(p.z);
        g_row[2]->setCaption(s.str());
    }
    {
        std::ostringstream s;
        if (haveRel) {
            s << "Na base dx/dz: " << static_cast<int>(rel.x) << " / "
              << static_cast<int>(rel.z) << " (dy " << static_cast<int>(rel.y) << ")";
        } else {
            s << "Na base: (predio sem town)";
        }
        g_row[3]->setCaption(s.str());
    }
    g_win->setVisible(true);

    std::ostringstream log;
    log << "PAINEL-PREDIO: \"" << name << "\" uid=" << uid << " mundo=("
        << static_cast<int>(p.x) << "," << static_cast<int>(p.y) << ","
        << static_cast<int>(p.z) << ")";
    diag::milestone(log.str());
    diag::flush();
}

} // namespace

void pollBuildingSelection(GameWorld* world) {
    if (world == 0 || world->player == 0) {
        return;
    }
    // selectedObject = ultimo objeto clicado (char/item/predio). getBuilding()
    // != 0 so quando e predio. Leitura barata por frame; agir so na MUDANCA.
    hand sel = world->player->selectedObject;
    if (!sel.isValid()) {
        return;
    }
    Building* b = sel.getBuilding();
    if (b == 0) {
        return; // clique nao foi num predio -> nada a fazer (janela fica)
    }
    // CHAVE de identidade do predio. Achado in-game 17/07: predios
    // ESTRUTURAIS ("Casa em L") NAO tem uid -- so os objetos/moveis colocados
    // (camas, maquinas) tem. E o hand::toString() e instavel entre frames
    // (re-disparava a mesma casa). A POSICAO e o fallback estavel: predio nao
    // se move; casas distintas ficam em pontos distintos. (Vale tambem p/ o
    // futuro sistema de declaracao por predio: uid quando ha, senao posicao.)
    std::string key;
    {
        InstanceID* iid = b->getInstanceID();
        if (iid != 0) {
            key = iid->uid;
        }
    }
    if (key.empty()) {
        Ogre::Vector3 p = b->getPosition();
        std::ostringstream k;
        k << "pos:" << static_cast<int>(p.x) << "," << static_cast<int>(p.y)
          << "," << static_cast<int>(p.z);
        key = k.str();
    }
    if (key == g_lastKey) {
        return; // mesmo predio ja tratado (mostrado ou fechado) -> nada a fazer
    }
    g_lastKey = key;
    if (key == g_closedKey) {
        return; // este e o predio que voce fechou e ainda esta selecionado
    }
    g_closedKey.clear(); // outro predio -> esquece a memoria de fechado
    showFor(b);
}

} // namespace ui
} // namespace ls
