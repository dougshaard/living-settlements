# tests/unit — pirâmide §16.1 nível 1 (núcleo puro fora do jogo)

O domínio (`plugin/src/domain`) não depende de KenshiLib/Ogre/Boost e
compila em qualquer toolchain.

## Rodar

```sh
# g++ / clang
g++ -std=c++03 -Wall -Wextra -I../../plugin/src \
    ../../plugin/src/domain/ReservationManager.cpp test_reservation.cpp \
    -o test_reservation && ./test_reservation

# MSVC (Developer Prompt)
cl /EHsc /W4 /I..\..\plugin\src ..\..\plugin\src\domain\ReservationManager.cpp test_reservation.cpp /Fe:test_reservation.exe && test_reservation.exe
```

Saída esperada: `N verificacoes, 0 falhas` + `OK` (exit code 0).

## Cobertura atual (test_reservation.cpp)

| Caso | Requisito/cenário do doc |
| --- | --- |
| available básico | REQ-LOG-002 |
| dois carregadores | TEST-003 |
| atomicidade do lote + item+capacidade | ADR-015, REQ-LOG-003 |
| agregação mesmo recurso (falha) | ADR-015 |
| fusão mesmo dono (contrato de unicidade) | PRINC-004 |
| dois donos no mesmo recurso | PRINC-004 |
| guarda de overflow (INT_MAX) | invariante §7.2 |
| expiração de lease | §6.4 regra 6 |
| expiração parcial multi-recurso | §6.4 regra 6 |
| release por dono | §7.2 (estados terminais liberam) |
| release individual / reaquisição | PRINC-005 |
| pedidos inválidos | robustez |
| reconciliação p/ baixo E p/ cima | TEST-007 (parcial) |
| version monotônica | entidade Reservation §7 |
| describe (básico + multi-recurso) | GOAL-007 / POC-010 |
| consultas inexistentes | robustez |

O contrato de unicidade (fusão por (recurso, dono)), a guarda de
overflow e o rollback sob exceção no commit são invariantes centrais
do `ReservationManager` e estão cobertos pelos casos acima.
