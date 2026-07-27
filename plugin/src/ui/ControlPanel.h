// Living Settlements -- ui/ControlPanel.h
// PAINEL DE CONTROLE in-game (GUI v1; diretriz 14 do dono: "a GUI e o
// principio de tudo"). Janela MyGUI com o skin NATIVO do Kenshi, criada no
// hook do TitleScreen (padrao do exemplo oficial KillButton). Toggles AO
// VIVO de orquestrador/guarnicao/POCs: a GUI so escreve no estado de
// toggles (pocEnvMutable); as POCs ja o releem a cada rodada -- nenhuma
// escrita de jogo acontece no callback de clique (mais estrito que o
// proprio exemplo oficial). poc.txt vira apenas o default de boot.
#ifndef LS_UI_CONTROLPANEL_H
#define LS_UI_CONTROLPANEL_H

namespace ls {
namespace ui {

// Instala o hook do TitleScreen que cria a janela. Falha NAO derruba o
// plugin (o mod segue funcional por poc.txt; so fica sem painel).
bool installControlPanel();

// Re-pinta os rotulos do painel com o estado corrente (contagens de
// carregadores/postos incluidas). Chamado pelo tick a cada rodada: o rotulo
// NUNCA pode mostrar numero velho (dir.20 -- GUI nao mente); clique deixou
// de ser a unica coisa que atualiza. Barato; no-op se o painel nao existe.
void refreshControlPanelCaptions();

} // namespace ui
} // namespace ls

#endif // LS_UI_CONTROLPANEL_H
