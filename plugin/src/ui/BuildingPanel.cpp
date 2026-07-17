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
std::string    g_lastKey; // uid do predio em exibicao ("" = nenhum/fechado)

void onWindowButton(MyGUI::Window* sender, const std::string& button) {
    // Botao de fechar (X) do skin CX manda "close". Esconder e esquecer o
    // predio atual -> clicar o MESMO predio de novo reabre.
    if (button == "close" && sender != 0) {
        sender->setVisible(false);
        g_lastKey.clear();
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
    std::string key;
    {
        InstanceID* iid = b->getInstanceID();
        if (iid != 0) {
            key = iid->uid;
        }
    }
    if (key.empty()) {
        key = sel.toString(); // predio em obra sem uid: chave pela referencia
    }
    if (key == g_lastKey) {
        return; // mesmo predio ja em exibicao -> nada a refazer
    }
    g_lastKey = key;
    showFor(b);
}

} // namespace ui
} // namespace ls
