// Living Settlements -- domain/IntentLedger.cpp
// Puro, C++03, ASCII-only.
#include "domain/IntentLedger.h"

namespace ls {
namespace domain {

IntentLedger::IntentLedger() {}

const StationView* IntentLedger::findStation(const WorldSnapshot& snap,
                                             const StationId& id) {
    for (std::size_t i = 0; i < snap.stations.size(); ++i) {
        if (snap.stations[i].id == id) {
            return &snap.stations[i];
        }
    }
    return 0;
}

bool IntentLedger::workerIsOperator(const StationView& s, const WorkerId& w) {
    for (std::size_t i = 0; i < s.operatorsNow.size(); ++i) {
        if (s.operatorsNow[i] == w) {
            return true;
        }
    }
    return false;
}

bool IntentLedger::hasActive(const WorkerId& worker, const TaskKey& task) const {
    for (std::size_t i = 0; i < intents_.size(); ++i) {
        if (intents_[i].isActive()
            && intents_[i].workerId == worker
            && intents_[i].taskKey == task) {
            return true;
        }
    }
    return false;
}

bool IntentLedger::taskHasActive(const TaskKey& task) const {
    for (std::size_t i = 0; i < intents_.size(); ++i) {
        if (intents_[i].isActive() && intents_[i].taskKey == task) {
            return true;
        }
    }
    return false;
}

void IntentLedger::record(const Intent& intent) {
    Intent copy = intent;
    copy.state = INTENT_PENDING;
    intents_.push_back(copy);
}

void IntentLedger::reconcile(const WorldSnapshot& snap, Tick now, Tick graceHours,
                             std::vector<std::string>& freedOwners) {
    for (std::size_t i = 0; i < intents_.size(); ++i) {
        Intent& in = intents_[i];
        if (in.isTerminal()) {
            continue;
        }
        const StationView* st = findStation(snap, in.targetStation);

        if (in.state == INTENT_PENDING) {
            if (in.verb == IV_ADD_ORDER) {
                if (st != 0 && workerIsOperator(*st, in.workerId)) {
                    in.state = INTENT_CONFIRMED;             // efeito apareceu
                } else if (now - in.emittedHours > graceHours) {
                    in.state = INTENT_FAILED;                // graca esgotada
                    freedOwners.push_back(in.reservationOwner);
                }
            } else { // IV_SET_DESTINATION: pre-posicionamento, so expira aqui
                if (now - in.emittedHours > graceHours) {
                    in.state = INTENT_FAILED;
                    freedOwners.push_back(in.reservationOwner);
                }
            }
        } else if (in.state == INTENT_CONFIRMED) {
            // Lacuna fechada: estacao sumiu, nao precisa mais operar, ou
            // foi dispensada -> trabalho concluido, libera a reserva.
            if (st == 0 || !st->needsOperating || st->dontNeedWork) {
                in.state = INTENT_DONE;
                freedOwners.push_back(in.reservationOwner);
            } else if (!workerIsOperator(*st, in.workerId)) {
                // Worker saiu (morte/KO/realocacao): reabrir a lacuna.
                in.state = INTENT_FAILED;
                freedOwners.push_back(in.reservationOwner);
            }
            // senao: continua operando -> mantem CONFIRMED e a reserva.
        }
    }
}

void IntentLedger::purgeTerminal() {
    std::vector<Intent> keep;
    keep.reserve(intents_.size());
    for (std::size_t i = 0; i < intents_.size(); ++i) {
        if (!intents_[i].isTerminal()) {
            keep.push_back(intents_[i]);
        }
    }
    intents_.swap(keep);
}

std::size_t IntentLedger::activeCount() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < intents_.size(); ++i) {
        if (intents_[i].isActive()) {
            ++n;
        }
    }
    return n;
}

std::size_t IntentLedger::count() const {
    return intents_.size();
}

void IntentLedger::clear() {
    intents_.clear();
}

} // namespace domain
} // namespace ls
