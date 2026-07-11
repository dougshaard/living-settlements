// Living Settlements -- domain/IntentLedger.h
// -----------------------------------------------------------------
// Memoria de escrita / verificar-antes-de-confiar (secoes 3.3, 7.4).
// Idempotencia + auto-cura: nunca reemite o que ja pediu; CONFIRMA
// quando o efeito esperado aparece no snapshot; marca FAILED apos a
// janela de graca; marca DONE quando a lacuna fecha; libera a reserva
// em todo estado terminal.
//
// Criterio de CONFIRM por verbo (secao 7.4):
//   addOrder(station, trabalho) -> worker in station.operatorsNow.
//   setDestination(pos)         -> pre-posicionamento; sem verify forte
//                                  aqui (chegada e do chamador); so expira.
//
// SHADOW-safe: em modo sombra os intents sao calculados/registrados mas
// nada e emitido; reconcile ainda roda para exercitar a logica e
// alimentar o painel "Por que". Puro (sem KenshiLib). C++03. ASCII-only.
// -----------------------------------------------------------------
#ifndef LS_DOMAIN_INTENTLEDGER_H
#define LS_DOMAIN_INTENTLEDGER_H

#include "domain/WorkModel.h"
#include <vector>
#include <string>
#include <cstddef>

namespace ls {
namespace domain {

class IntentLedger {
public:
    IntentLedger();

    // Dedup (invariante 13): ja existe intent ATIVO (PENDING/CONFIRMED)
    // para este par (worker, tarefa) ou para esta tarefa (qualquer worker)?
    bool hasActive(const WorkerId& worker, const TaskKey& task) const;
    bool taskHasActive(const TaskKey& task) const;

    // Registra um intent novo (estado inicial PENDING).
    void record(const Intent& intent);

    // Reconcilia contra o snapshot. Transicoes:
    //   PENDING(addOrder)  -> CONFIRMED  se worker in operatorsNow;
    //                      -> FAILED     se now-emitido > graca.
    //   PENDING(setDest)   -> FAILED     se now-emitido > graca (expira).
    //   CONFIRMED          -> DONE       se a estacao nao precisa mais de
    //                                    operador (trabalho concluido);
    //                      -> FAILED     se o worker deixou de operar.
    // Anexa a freedOwners o reservationOwner de cada intent que virou
    // terminal (o chamador libera no ReservationManager).
    void reconcile(const WorldSnapshot& snap, Tick now, Tick graceHours,
                   std::vector<std::string>& freedOwners);

    // Remove os intents terminais (FAILED/DONE) apos processados.
    void purgeTerminal();

    std::size_t activeCount() const;
    std::size_t count() const;
    const std::vector<Intent>& intents() const { return intents_; }
    void clear(); // reset total (secao 7.5: chamar no LOAD)

private:
    std::vector<Intent> intents_;
    static const StationView* findStation(const WorldSnapshot& snap,
                                          const StationId& id);
    static bool workerIsOperator(const StationView& s, const WorkerId& w);
};

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_INTENTLEDGER_H
