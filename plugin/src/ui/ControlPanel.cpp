// Living Settlements -- ui/ControlPanel.cpp
// GUI v1 (painel de controle). ASCII-only. Padrao COPIADO do exemplo oficial
// KillButton (KenshiLib_Examples): hook em TitleScreen::_CONSTRUCTOR
// (TitleScreen.h:22), MyGUI::Gui::getInstancePtr, createWidgetReal com os
// skins nativos "Kenshi_WindowCX"/"Kenshi_Button1", eventMouseButtonClick.
// O callback de clique SO flipa toggles do mod (nenhuma escrita de jogo);
// as POCs/feicoes agem no proximo tick, atras da cerca de sempre.
#include "ui/ControlPanel.h"
#include "core/PocEnv.h"
#include "core/Diagnostics.h"

#include <kenshi/gui/TitleScreen.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>

#include <core/Functions.h>   // KenshiLib::AddHook / GetRealAddress

#include <string>

namespace ls {
namespace ui {

namespace {

MyGUI::Button* g_btnOrch = 0;
MyGUI::Button* g_btnGarrison = 0;
MyGUI::Button* g_btnHaul = 0;
MyGUI::Button* g_btnHaulOnce = 0;
MyGUI::Button* g_btnMed = 0;
MyGUI::Button* g_btnTur = 0;
MyGUI::Button* g_btnWipe = 0;

std::string label(const char* name, bool on) {
    return std::string(name) + ": " + (on ? "LIGADO" : "desligado");
}

void refreshCaptions() {
    const core::PocEnvState& e = core::pocEnv();
    if (g_btnOrch != 0) {
        g_btnOrch->setCaption(label("Orquestrador", e.orchestrator));
    }
    if (g_btnGarrison != 0) {
        g_btnGarrison->setCaption(label("Guarnicao", e.garrison));
    }
    if (g_btnHaul != 0) {
        g_btnHaul->setCaption(label("Carregador", e.haul));
    }
    if (g_btnHaulOnce != 0) {
        g_btnHaulOnce->setCaption(e.haulOnce ? "Carregador: 1 ciclo (pedido)"
                                             : "Carregador: 1 ciclo");
    }
    if (g_btnMed != 0) {
        g_btnMed->setCaption(label("Medicos", e.medicRole));
    }
    if (g_btnTur != 0) {
        g_btnTur->setCaption(label("Torre (obs)", e.turEnabled));
    }
    if (g_btnWipe != 0) {
        g_btnWipe->setCaption(e.clearJobs ? "Limpar cargos: LIMPANDO..."
                                          : "Limpar todos os cargos");
    }
}

void onToggle(MyGUI::WidgetPtr sender) {
    core::PocEnvState& e = core::pocEnvMutable();
    const char* what = "?";
    bool now = false;
    if (sender == g_btnOrch) {
        e.orchestrator = !e.orchestrator;
        what = "Orquestrador";
        now = e.orchestrator;
    } else if (sender == g_btnGarrison) {
        e.garrison = !e.garrison;
        what = "Guarnicao";
        now = e.garrison;
    } else if (sender == g_btnHaul) {
        e.haul = !e.haul;
        what = "Carregador (laco)";
        now = e.haul;
    } else if (sender == g_btnHaulOnce) {
        e.haulOnce = true; // a POC consome no proximo tick (dir.14)
        what = "Carregador (1 ciclo)";
        now = true;
    } else if (sender == g_btnMed) {
        e.medicRole = !e.medicRole;
        what = "Medicos (papel)";
        now = e.medicRole;
    } else if (sender == g_btnTur) {
        e.turEnabled = !e.turEnabled;
        what = "Torre (obs)";
        now = e.turEnabled;
    } else if (sender == g_btnWipe) {
        e.clearJobs = true; // a limpeza roda no proximo tick, atras da cerca,
                            // e desarma sozinha ao concluir
        what = "Limpar todos os cargos";
        now = true;
    }
    refreshCaptions();
    diag::milestone(std::string("PAINEL: ") + what + " -> "
                    + (now ? "LIGADO" : "desligado") + " (ao vivo)");
    diag::flush();
}

MyGUI::Button* makeToggle(MyGUI::Widget* parent, float y, const char* name) {
    MyGUI::Button* b = parent->createWidgetReal<MyGUI::Button>(
        "Kenshi_Button1", 0.05f, y, 0.90f, 0.12f, MyGUI::Align::Default, name);
    b->eventMouseButtonClick += MyGUI::newDelegate(onToggle);
    return b;
}

// Hook do construtor do TitleScreen (mesmo ponto do exemplo oficial: a UI
// do jogo ja esta de pe). Recria a janela a cada title screen, como o
// exemplo (sem guard: o teardown da UI entre sessoes destroi os widgets).
TitleScreen* (*g_titleOrig)(TitleScreen*) = 0;

TitleScreen* titleHook(TitleScreen* thisptr) {
    TitleScreen* ts = g_titleOrig(thisptr);
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) {
        return ts;
    }
    MyGUI::Window* w = gui->createWidgetReal<MyGUI::Window>(
        "Kenshi_WindowCX", 0.78f, 0.24f, 0.20f, 0.46f,
        MyGUI::Align::Default, "Window", "LSControlPanel");
    w->setCaption("Living Settlements");
    MyGUI::Widget* c = w->getClientWidget();
    g_btnOrch     = makeToggle(c, 0.02f, "LSBtnOrch");
    g_btnGarrison = makeToggle(c, 0.16f, "LSBtnGarrison");
    g_btnHaul     = makeToggle(c, 0.30f, "LSBtnHaul");
    g_btnHaulOnce = makeToggle(c, 0.44f, "LSBtnHaulOnce");
    g_btnMed      = makeToggle(c, 0.58f, "LSBtnMed");
    g_btnTur      = makeToggle(c, 0.72f, "LSBtnTur");
    g_btnWipe     = makeToggle(c, 0.86f, "LSBtnWipe");
    refreshCaptions();
    diag::milestone("PAINEL de controle criado (janela 'Living Settlements'; "
                    "toggles ao vivo -- poc.txt e so o default de boot)");
    return ts;
}

} // namespace

bool installControlPanel() {
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&TitleScreen::_CONSTRUCTOR),
            titleHook, &g_titleOrig)) {
        diag::error("PAINEL: falha ao instalar o hook do TitleScreen -- "
                    "seguindo sem GUI (poc.txt continua valendo)");
        return false;
    }
    diag::milestone("PAINEL: hook do TitleScreen instalado");
    return true;
}

} // namespace ui
} // namespace ls
