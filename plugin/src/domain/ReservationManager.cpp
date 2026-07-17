// Living Settlements — domain/ReservationManager.cpp
// Ver contrato e rastreabilidade de requisitos no header.
#include "ReservationManager.h"

#include <sstream>
#include <algorithm>
#include <climits>
#include <utility>

namespace ls {
namespace domain {

ReservationManager::ReservationManager() {}

void ReservationManager::setPhysical(const ResourceKey& resource, int physicalQty) {
    if (physicalQty < 0)
        physicalQty = 0;
    Entry& e = table_[resource];
    if (e.physicalQty != physicalQty) {
        e.physicalQty = physicalQty;
        e.version++;
    }
}

int ReservationManager::physical(const ResourceKey& resource) const {
    Table::const_iterator it = table_.find(resource);
    return (it == table_.end()) ? 0 : it->second.physicalQty;
}

int ReservationManager::reservedOf(const Entry& e) const {
    int total = 0;
    for (size_t i = 0; i < e.leases.size(); ++i)
        total += e.leases[i].quantity;
    return total;
}

int ReservationManager::reserved(const ResourceKey& resource) const {
    Table::const_iterator it = table_.find(resource);
    return (it == table_.end()) ? 0 : reservedOf(it->second);
}

int ReservationManager::available(const ResourceKey& resource) const {
    // REQ-LOG-002: disponível = físico − reservado, saturado em zero.
    Table::const_iterator it = table_.find(resource);
    if (it == table_.end())
        return 0;
    int avail = it->second.physicalQty - reservedOf(it->second);
    return (avail > 0) ? avail : 0;
}

bool ReservationManager::acquireAtomic(const std::vector<ReservationRequest>& requests, Tick now) {
    if (requests.empty())
        return false;

    // Fase 1 — validar tudo, sem tocar no estado (ADR-015).
    // Agregação em dois níveis: por recurso (dois pedidos do mesmo lote
    // sobre o mesmo recurso competem pela MESMA quantidade disponível) e
    // por (recurso, dono) (contrato de unicidade: pedidos do mesmo dono
    // fundem quantity/expiresAt). std::map itera em ordem de chave =>
    // ordem canônica determinística.
    typedef std::map<OwnerKey, MergeReq> OwnerMap;
    typedef std::map<ResourceKey, OwnerMap> WantedMap;
    WantedMap wanted;

    for (size_t i = 0; i < requests.size(); ++i) {
        const ReservationRequest& r = requests[i];
        if (r.quantity <= 0)
            return false; // pedido malformado
        if (r.expiresAt <= now)
            return false; // lease sem validade futura (§6.4 regra 6)
        if (r.resource.empty() || r.owner.empty())
            return false;

        OwnerMap& owners = wanted[r.resource];
        OwnerMap::iterator oit = owners.find(r.owner);
        if (oit == owners.end()) {
            MergeReq m;
            m.quantity = r.quantity;
            m.expiresAt = r.expiresAt;
            owners.insert(std::make_pair(r.owner, m));
        } else {
            // Guarda de overflow (achado da revisão): sem ela, a soma
            // pode envolver para negativo e a validação passar.
            if (r.quantity > INT_MAX - oit->second.quantity)
                return false;
            oit->second.quantity += r.quantity;
            if (r.expiresAt > oit->second.expiresAt)
                oit->second.expiresAt = r.expiresAt;
        }
    }

    for (WantedMap::const_iterator it = wanted.begin(); it != wanted.end(); ++it) {
        int totalForResource = 0;
        for (OwnerMap::const_iterator oit = it->second.begin();
             oit != it->second.end(); ++oit) {
            if (oit->second.quantity > INT_MAX - totalForResource)
                return false; // overflow entre donos distintos
            totalForResource += oit->second.quantity;
            // Fusão com lease PRÉ-EXISTENTE do mesmo dono também não
            // pode estourar: novo_total = existente + pedido.
            Table::const_iterator te = table_.find(it->first);
            if (te != table_.end()) {
                for (size_t li = 0; li < te->second.leases.size(); ++li) {
                    if (te->second.leases[li].owner == oit->first) {
                        if (oit->second.quantity >
                            INT_MAX - te->second.leases[li].quantity)
                            return false;
                        break;
                    }
                }
            }
        }
        if (available(it->first) < totalForResource)
            return false; // insuficiente: nada foi alterado
    }

    // Fase 2 — efetivar, com journal de rollback: se qualquer alocação
    // lançar no meio do commit (OOM), desfazemos tudo e retornamos
    // false — a atomicidade do ADR-015 vale também sob exceção
    // (achado da revisão: o push_back podia deixar commit parcial).
    std::vector<UndoRec> journal;

    try {
        for (WantedMap::const_iterator it = wanted.begin(); it != wanted.end(); ++it) {
            // Validação garante que o recurso existe (available()>0 exige
            // entry com físico); find nunca cria entradas novas aqui.
            Table::iterator te = table_.find(it->first);
            Entry& e = te->second;

            for (OwnerMap::const_iterator oit = it->second.begin();
                 oit != it->second.end(); ++oit) {
                // Localiza lease existente do dono (contrato de unicidade).
                Lease* existing = 0;
                for (size_t li = 0; li < e.leases.size(); ++li) {
                    if (e.leases[li].owner == oit->first) {
                        existing = &e.leases[li];
                        break;
                    }
                }

                // Journal ANTES de mutar (as alocações perigosas — cópias
                // de string, growth do vector — acontecem aqui; se
                // lançarem, a mutação deste par ainda não ocorreu).
                UndoRec u;
                u.entry = &e;
                u.owner = oit->first;
                u.existedBefore = (existing != 0);
                u.oldQty = existing ? existing->quantity : 0;
                u.oldExp = existing ? existing->expiresAt : 0.0;
                u.oldVersion = e.version;
                journal.push_back(u);

                if (existing != 0) {
                    // Fusão (nothrow): soma validada contra overflow na fase 1.
                    existing->quantity += oit->second.quantity;
                    if (oit->second.expiresAt > existing->expiresAt)
                        existing->expiresAt = oit->second.expiresAt;
                    e.version++;
                    existing->version = e.version;
                } else {
                    Lease lease;
                    lease.owner = oit->first;
                    lease.quantity = oit->second.quantity;
                    lease.expiresAt = oit->second.expiresAt;
                    lease.version = e.version + 1;
                    e.leases.push_back(lease); // pode lançar — coberto pelo journal
                    e.version++;
                }
            }
        }
    } catch (...) {
        // Rollback nothrow: desfaz na ordem inversa.
        for (size_t i = journal.size(); i > 0; --i) {
            UndoRec& u = journal[i - 1];
            std::vector<Lease>& leases = u.entry->leases;
            for (size_t li = 0; li < leases.size(); ++li) {
                if (leases[li].owner == u.owner) {
                    if (u.existedBefore) {
                        leases[li].quantity = u.oldQty;
                        leases[li].expiresAt = u.oldExp;
                    } else {
                        leases.erase(leases.begin() + li);
                    }
                    break;
                }
            }
            u.entry->version = u.oldVersion;
        }
        return false;
    }
    return true;
}

bool ReservationManager::release(const ResourceKey& resource, const OwnerKey& owner) {
    // Contrato de unicidade: há no máximo UM lease por (recurso, dono)
    // — o primeiro match é o único.
    Table::iterator it = table_.find(resource);
    if (it == table_.end())
        return false;
    std::vector<Lease>& leases = it->second.leases;
    for (size_t i = 0; i < leases.size(); ++i) {
        if (leases[i].owner == owner) {
            leases.erase(leases.begin() + i);
            it->second.version++;
            return true;
        }
    }
    return false;
}

int ReservationManager::releaseOwner(const OwnerKey& owner) {
    // §7.2: todo estado terminal libera seus ativos.
    int released = 0;
    for (Table::iterator it = table_.begin(); it != table_.end(); ++it) {
        std::vector<Lease>& leases = it->second.leases;
        size_t i = 0;
        while (i < leases.size()) {
            if (leases[i].owner == owner) {
                leases.erase(leases.begin() + i);
                it->second.version++;
                released++;
            } else {
                ++i;
            }
        }
    }
    return released;
}

int ReservationManager::expire(Tick now) {
    // §6.4 regra 6: nenhuma reserva sobrevive ao próprio lease.
    int released = 0;
    for (Table::iterator it = table_.begin(); it != table_.end(); ++it) {
        std::vector<Lease>& leases = it->second.leases;
        size_t i = 0;
        while (i < leases.size()) {
            if (leases[i].expiresAt <= now) {
                leases.erase(leases.begin() + i);
                it->second.version++;
                released++;
            } else {
                ++i;
            }
        }
    }
    return released;
}

int ReservationManager::touch(const OwnerKey& owner, Tick newExpiry) {
    int touched = 0;
    for (Table::iterator it = table_.begin(); it != table_.end(); ++it) {
        std::vector<Lease>& leases = it->second.leases;
        for (size_t i = 0; i < leases.size(); ++i) {
            if (leases[i].owner == owner && leases[i].expiresAt < newExpiry) {
                leases[i].expiresAt = newExpiry;
                it->second.version++;
                ++touched;
            }
        }
    }
    return touched;
}

bool ReservationManager::ownerHasLeases(const OwnerKey& owner) const {
    for (Table::const_iterator it = table_.begin(); it != table_.end(); ++it) {
        const std::vector<Lease>& leases = it->second.leases;
        for (size_t i = 0; i < leases.size(); ++i)
            if (leases[i].owner == owner)
                return true;
    }
    return false;
}

void ReservationManager::describe(std::vector<std::string>& out) const {
    // GOAL-007 / POC-010: uma linha legível por lease ativo.
    for (Table::const_iterator it = table_.begin(); it != table_.end(); ++it) {
        const Entry& e = it->second;
        for (size_t i = 0; i < e.leases.size(); ++i) {
            std::ostringstream line;
            line << it->first
                 << " qty=" << e.leases[i].quantity
                 << " owner=" << e.leases[i].owner
                 << " expira=" << e.leases[i].expiresAt
                 << " v" << e.leases[i].version
                 << " (fisico=" << e.physicalQty
                 << " reservado=" << reservedOf(e) << ")";
            out.push_back(line.str());
        }
    }
}

size_t ReservationManager::leaseCount() const {
    size_t n = 0;
    for (Table::const_iterator it = table_.begin(); it != table_.end(); ++it)
        n += it->second.leases.size();
    return n;
}

void ReservationManager::clear() {
    table_.clear();
}

} // namespace domain
} // namespace ls
