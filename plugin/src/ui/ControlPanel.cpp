// Living Settlements -- ui/ControlPanel.cpp
// GUI v1 (painel de controle). ASCII-only. Padrao COPIADO do exemplo oficial
// KillButton (KenshiLib_Examples): hook em TitleScreen::_CONSTRUCTOR
// (TitleScreen.h:22), MyGUI::Gui::getInstancePtr, createWidgetReal com os
// skins nativos "Kenshi_WindowCX"/"Kenshi_Button1", eventMouseButtonClick.
// O callback de clique SO flipa toggles do mod (nenhuma escrita de jogo);
// as POCs/feicoes agem no proximo tick, atras da cerca de sempre.
#include "ui/ControlPanel.h"
#include "core/PocEnv.h"
#include "core/Porters.h"
#include "core/Diagnostics.h"

#include <kenshi/gui/TitleScreen.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>

#include <core/Functions.h>   // KenshiLib::AddHook / GetRealAddress

#include <string>
#include <sstream>

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
MyGUI::Button* g_btnPorters = 0;

// --- Janela "Carregadores": roster paginado, clique alterna a declaracao.
// So skins ja provadas (Kenshi_WindowCX/Kenshi_Button1). A GUI le apenas o
// ESPELHO do roster (core/Porters, refrescado pelo tick) e so muta estado do
// mod -- nenhuma leitura/escrita de jogo no clique (padrao do painel v1).
static const int PORTER_PAGE = 10;
MyGUI::Window* g_porterWin = 0;
MyGUI::Button* g_porterBtn[PORTER_PAGE] = { 0 };
MyGUI::Button* g_porterPrev = 0;
MyGUI::Button* g_porterNext = 0;
int g_porterPage = 0;
int g_porterIdx[PORTER_PAGE]; // indice no espelho; -1 = linha vazia

void refreshPorterWindow() {
    if (g_porterWin == 0) {
        return;
    }
    const std::vector<core::RosterEntry>& r = core::roster();
    int n = static_cast<int>(r.size());
    int pages = (n + PORTER_PAGE - 1) / PORTER_PAGE;
    if (pages < 1) {
        pages = 1;
    }
    if (g_porterPage >= pages) {
        g_porterPage = pages - 1;
    }
    if (g_porterPage < 0) {
        g_porterPage = 0;
    }
    {
        std::ostringstream cap;
        cap << "Carregadores " << core::porterCount() << " (pag "
            << (g_porterPage + 1) << "/" << pages << ")";
        g_porterWin->setCaption(cap.str());
    }
    for (int i = 0; i < PORTER_PAGE; ++i) {
        if (g_porterBtn[i] == 0) {
            continue;
        }
        int idx = g_porterPage * PORTER_PAGE + i;
        if (idx < n) {
            g_porterIdx[i] = idx;
            g_porterBtn[i]->setCaption(
                std::string(r[idx].porter ? "[C] " : "     ") + r[idx].name);
        } else {
            g_porterIdx[i] = -1;
            g_porterBtn[i]->setCaption(n == 0 ? "(carregue um mundo)" : "-");
        }
    }
}

void onPorterRow(MyGUI::WidgetPtr sender) {
    const std::vector<core::RosterEntry>& r = core::roster();
    for (int i = 0; i < PORTER_PAGE; ++i) {
        if (sender == g_porterBtn[i]) {
            int idx = g_porterIdx[i];
            if (idx >= 0 && idx < static_cast<int>(r.size())) {
                core::togglePorter(r[idx].h);
                refreshPorterWindow();
            }
            return;
        }
    }
}

void onPorterNav(MyGUI::WidgetPtr sender) {
    if (sender == g_porterPrev) {
        --g_porterPage;
    } else if (sender == g_porterNext) {
        ++g_porterPage;
    }
    refreshPorterWindow();
}

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
    if (g_btnPorters != 0) {
        std::ostringstream cap;
        cap << "Carregadores: " << core::porterCount();
        g_btnPorters->setCaption(cap.str());
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
    } else if (sender == g_btnPorters) {
        if (g_porterWin != 0) {
            bool show = !g_porterWin->getVisible();
            g_porterWin->setVisible(show);
            if (show) {
                refreshPorterWindow();
            }
        }
        what = "Carregadores (aba)";
        now = (g_porterWin != 0 && g_porterWin->getVisible());
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
        "Kenshi_WindowCX", 0.78f, 0.22f, 0.20f, 0.50f,
        MyGUI::Align::Default, "Window", "LSControlPanel");
    w->setCaption("Living Settlements");
    MyGUI::Widget* c = w->getClientWidget();
    g_btnOrch     = makeToggle(c, 0.010f, "LSBtnOrch");
    g_btnGarrison = makeToggle(c, 0.135f, "LSBtnGarrison");
    g_btnHaul     = makeToggle(c, 0.260f, "LSBtnHaul");
    g_btnHaulOnce = makeToggle(c, 0.385f, "LSBtnHaulOnce");
    g_btnMed      = makeToggle(c, 0.510f, "LSBtnMed");
    g_btnTur      = makeToggle(c, 0.635f, "LSBtnTur");
    g_btnWipe     = makeToggle(c, 0.760f, "LSBtnWipe");
    g_btnPorters  = makeToggle(c, 0.885f, "LSBtnPorters");

    // Janela "Carregadores" (aba de declaracao; comeca oculta).
    g_porterWin = gui->createWidgetReal<MyGUI::Window>(
        "Kenshi_WindowCX", 0.56f, 0.22f, 0.21f, 0.50f,
        MyGUI::Align::Default, "Window", "LSPorterWindow");
    g_porterWin->setCaption("Carregadores");
    {
        MyGUI::Widget* pc = g_porterWin->getClientWidget();
        for (int i = 0; i < PORTER_PAGE; ++i) {
            g_porterIdx[i] = -1;
            g_porterBtn[i] = pc->createWidgetReal<MyGUI::Button>(
                "Kenshi_Button1", 0.03f, 0.010f + 0.082f * i, 0.94f, 0.075f,
                MyGUI::Align::Default, "");
            g_porterBtn[i]->eventMouseButtonClick += MyGUI::newDelegate(onPorterRow);
        }
        g_porterPrev = pc->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1", 0.03f, 0.845f, 0.45f, 0.10f,
            MyGUI::Align::Default, "");
        g_porterPrev->setCaption("< Anterior");
        g_porterPrev->eventMouseButtonClick += MyGUI::newDelegate(onPorterNav);
        g_porterNext = pc->createWidgetReal<MyGUI::Button>(
            "Kenshi_Button1", 0.52f, 0.845f, 0.45f, 0.10f,
            MyGUI::Align::Default, "");
        g_porterNext->setCaption("Proxima >");
        g_porterNext->eventMouseButtonClick += MyGUI::newDelegate(onPorterNav);
    }
    g_porterWin->setVisible(false);
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
