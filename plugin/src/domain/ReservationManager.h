// Living Settlements — domain/ReservationManager.h
// ------------------------------------------------------------------
// Núcleo net-new do projeto (ADR-007 reservas obrigatórias; ADR-015
// reserva como camada externa sobre handles). Não existe primitiva de
// reserva nativa no Kenshi (FACT-014) — esta é a peça diferencial.
//
// Regras implementadas:
//   REQ-LOG-002  disponível = físico − reservado
//   §6.4 regra 6 nenhuma reserva sem lease e expiração
//   §7.2         soma de reservas nunca excede a quantidade física;
//                todo estado terminal libera seus ativos
//   ADR-015      chaves são ids estáveis (hand::toString / InstanceID),
//                aquisição tudo-ou-nada com ordem canônica + rollback
//
// Threading: este componente é single-threaded POR DESENHO. Todo o
// ciclo do coordenador roda no tick da main thread (ADR-014); nenhuma
// sincronização é necessária aqui. NÃO chamar de worker threads.
//
// Pureza: sem dependência de KenshiLib/Ogre/Boost — compila em
// qualquer toolchain (pirâmide de testes §16.1, nível 1). Código
// C++03-conservador para compatibilidade com o toolset VS2010
// (RISK-009).
// ------------------------------------------------------------------
#ifndef LS_DOMAIN_RESERVATIONMANAGER_H
#define LS_DOMAIN_RESERVATIONMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace ls {
namespace domain {

// Ids estáveis, engine-agnósticos. O adapter converte hand/InstanceID
// para estas strings (ADR-015); o domínio nunca vê ponteiros do jogo
// (REQ-PER-001: nenhum ponteiro persistido).
typedef std::string ResourceKey; // ex.: "item:<uid>:<building-uid>", "cap:<building-uid>", "bed:<uid>"
typedef std::string OwnerKey;    // ex.: "task:<id>" — um único dono lógico por reserva (PRINC-004)

// Tempo lógico em horas de jogo. O chamador injeta o relógio
// (GameWorld::getTimeStamp_inGameHours no runtime; valor sintético nos
// testes) — o domínio permanece determinístico (PRINC-007, §16.1).
typedef double Tick;

struct ReservationRequest {
    ResourceKey resource;
    OwnerKey    owner;
    int         quantity;  // > 0
    Tick        expiresAt; // lease obrigatório (§6.4 regra 6)

    ReservationRequest() : quantity(0), expiresAt(0.0) {}
    ReservationRequest(const ResourceKey& r, const OwnerKey& o, int q, Tick e)
        : resource(r), owner(o), quantity(q), expiresAt(e) {}
};

struct Lease {
    OwnerKey owner;
    int      quantity;
    Tick     expiresAt;
    unsigned version; // incrementa a cada mudança do recurso (entidade Reservation, §7)

    Lease() : quantity(0), expiresAt(0.0), version(0) {}
};

class ReservationManager {
public:
    ReservationManager();

    // Reconciliação: o snapshot informa a quantidade física observada.
    // Se físico < reservado (mundo mudou por fora), as reservas ficam
    // temporariamente acima do físico; available() satura em zero e a
    // reconciliação de leases é responsabilidade do ciclo de
    // replanejamento (TEST-007: reservas inválidas não persistem).
    void setPhysical(const ResourceKey& resource, int physicalQty);

    int physical(const ResourceKey& resource) const;
    int reserved(const ResourceKey& resource) const;
    // REQ-LOG-002: max(0, físico − reservado)
    int available(const ResourceKey& resource) const;

    // Aquisição tudo-ou-nada (ADR-015). Valida TODOS os pedidos contra
    // o estado corrente (agregando pedidos que apontam para o mesmo
    // recurso, com guarda de overflow) e só então efetiva. Em caso de
    // qualquer insuficiência — ou de exceção durante o commit (OOM) —
    // NADA muda e retorna false (rollback interno). A validação percorre
    // os recursos em ordem canônica (ordenação de ResourceKey).
    //
    // CONTRATO DE UNICIDADE (decisão de design): existe no
    // máximo UM lease por par (recurso, dono). Pedidos do mesmo dono
    // para o mesmo recurso são FUNDIDOS: quantity soma, expiresAt fica
    // o maior. Isso torna release(recurso, dono) não-ambíguo e casa com
    // PRINC-004 (um proprietário lógico por reserva).
    //
    // Pedidos com quantity <= 0 ou expiresAt <= now são rejeitados
    // (lease inválido, §6.4 regra 6).
    bool acquireAtomic(const std::vector<ReservationRequest>& requests, Tick now);

    // Libera O lease do par (recurso, dono) — único pelo contrato de
    // unicidade. Retorna false se não existir.
    bool release(const ResourceKey& resource, const OwnerKey& owner);

    // Todo estado terminal libera seus ativos (§7.2): solta TODOS os
    // leases do dono. Retorna quantos leases foram liberados.
    int releaseOwner(const OwnerKey& owner);

    // Expira leases vencidos (expiresAt <= now). Retorna quantos
    // foram liberados. Chamar a cada tick do coordenador.
    int expire(Tick now);

    // RENOVA (estende) todos os leases do dono para expiresAt = max(atual,
    // newExpiry), sem alterar quantidade -- ao contrario de acquireAtomic, que
    // FUNDE (soma) a quantidade. Serve para manter viva a reserva de uma tarefa
    // em ANDAMENTO (ex.: carregador numa caminhada longa) enquanto o dono ainda
    // a controla; expire() so recolhe orfaos que ninguem renova. Retorna
    // quantos leases foram tocados.
    int touch(const OwnerKey& owner, Tick newExpiry);

    // Falha nunca deixa reserva presa (PRINC-005) — releaseOwner é o
    // caminho; este utilitário verifica o invariante em testes.
    bool ownerHasLeases(const OwnerKey& owner) const;

    // Introspecção p/ o painel "Por quê?" e diagnóstico (GOAL-007,
    // POC-010): uma linha legível por lease ativo.
    void describe(std::vector<std::string>& out) const;

    size_t leaseCount() const;
    void clear();

private:
    struct Entry {
        int physicalQty;
        unsigned version;
        std::vector<Lease> leases;
        Entry() : physicalQty(0), version(0) {}
    };
    typedef std::map<ResourceKey, Entry> Table;

    // Auxiliares de acquireAtomic. Tipos ANINHADOS (não locais) de
    // propósito: C++03 proíbe tipos locais como argumento de template
    // (std::map/std::vector) — o g++ -std=c++03 dos testes rejeitaria.
    struct MergeReq {
        int quantity;
        Tick expiresAt;
        MergeReq() : quantity(0), expiresAt(0.0) {}
    };
    struct UndoRec {
        Entry* entry;
        OwnerKey owner;
        bool existedBefore;
        int oldQty;
        Tick oldExp;
        unsigned oldVersion;
        UndoRec() : entry(0), existedBefore(false), oldQty(0),
                    oldExp(0.0), oldVersion(0) {}
    };

    Table table_;

    int reservedOf(const Entry& e) const;

    // não copiável (posse única do estado de reservas)
    ReservationManager(const ReservationManager&);
    ReservationManager& operator=(const ReservationManager&);
};

} // namespace domain
} // namespace ls

#endif // LS_DOMAIN_RESERVATIONMANAGER_H
