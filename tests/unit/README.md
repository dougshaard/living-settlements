# tests/unit

Testes do núcleo puro (`plugin/src/domain`). Ele não depende do KenshiLib nem do jogo,
então compila e roda em qualquer toolchain.

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

## O que os testes cobrem

`test_reservation.cpp` — o gerenciador de reservas:

- reserva disponível básica e com dois carregadores
- aquisição atômica do lote (tudo-ou-nada), incluindo item + capacidade juntos
- agregação e fusão de pedidos do mesmo dono no mesmo recurso
- dois donos disputando o mesmo recurso
- guarda de overflow (INT_MAX)
- expiração de lease, inclusive parcial e multi-recurso
- liberação por dono e reaquisição individual
- pedidos inválidos e consultas a recursos inexistentes
- reconciliação do físico pra cima e pra baixo
- versão monotônica da reserva
- descrição legível pro log

O contrato de unicidade (um lease por par recurso/dono), a guarda de overflow e o
rollback sob exceção no commit são os invariantes centrais do gerenciador, e estão
cobertos pelos casos acima. Os demais arquivos (`test_workcore`, `test_taskboard`,
`test_intent`, `test_assignment`) cobrem o resto do núcleo — modelo de dados, quadro de
tarefas, pool de trabalhadores, debounce, ledger de intenções e a atribuição.
