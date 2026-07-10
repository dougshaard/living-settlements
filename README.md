# Living Settlements — plugin (Etapa 0: prova técnica)

Mod de IA e automação de assentamentos para **Kenshi**. Camada de coordenação
de colônia (reservas, logística pull, política de estoque, atribuição global)
por cima da IA GOAP nativa do jogo, via **RE_Kenshi/KenshiLib**.

- Este repositório implementa a **Etapa 0** do roadmap: infraestrutura do
  plugin, POCs de leitura (POC-001..003) e o núcleo de reservas testável,
  com gate de arquitetura A/B/C.
- Licença: **GPLv3** (obrigatória — KenshiLib é GPLv3). Ver [LICENSE](LICENSE).

## Estado

| Peça | Status |
| --- | --- |
| Domínio puro (`plugin/src/domain`) — ReservationManager | Implementado; contrato de unicidade, guarda de overflow e rollback sob exceção |
| Testes unitários (`tests/unit`) | 16 casos, 0 falhas; compilam em qualquer toolchain, sem o jogo |
| Plugin (entry, version gate c/ allowlist, tick, diagnóstico) | Compila em **v100 / Release \| x64** → `LivingSettlements.dll` (exporta `?startPlugin@@YAXXZ`) |
| POC-001/002/003 (leitura: personagens+GOAP, base, ameaça/atacante) | Implementadas e validadas em jogo (Kenshi 1.0.65 via RE_Kenshi) |
| Empacotamento (`plugin/package`) | RE_Kenshi.json + LivingSettlements.mod (46 bytes, byte-idêntico ao formato oficial) |
| POC-011 (write-path mínimo) | Implementada, **desligada por padrão** (`LsConfig.h`) |
| POCs restantes | Próximas; a infra (tick/diagnóstico/gate) já as suporta |

## Pré-requisitos de build (RISK-009 — toolchain antigo)

1. **Visual Studio 2019+** com o toolset **Visual C++ 2010 (v100)** instalado
   (o binário do Kenshi exige ABI VS2010; ver README do KenshiLib).
2. **KenshiLib_Examples_deps** clonado como **irmão** desta pasta
   (`../KenshiLib_Examples_deps`): contém os includes do KenshiLib, Ogre,
   MyGUI e o Boost 1.60 pré-compilado.
3. Kenshi 1.0.65/1.0.68 (Steam ou GOG) + **RE_Kenshi 0.3.4+** instalado.

## Build

1. Abrir `plugin/LivingSettlements.sln` no Visual Studio.
2. Configuração **Release | x64** (Debug não é suportado pelo KenshiLib).
3. Compilar. A DLL sai em `plugin/x64/Release/LivingSettlements.dll`.

### Testes do domínio (qualquer máquina, sem jogo)

```
cd tests/unit
g++ -std=c++03 -Wall -I../../plugin/src ../../plugin/src/domain/ReservationManager.cpp test_reservation.cpp -o test_reservation && ./test_reservation
```

(ou `cl /EHsc /W4 /I..\..\plugin\src ...` no MSVC)

## Instalação no jogo

1. Criar no FCS um mod vazio chamado **LivingSettlements** (gera
   `LivingSettlements.mod` na pasta `mods/LivingSettlements/` do Kenshi).
2. Copiar para essa pasta: `LivingSettlements.dll` (do build) e
   `plugin/package/LivingSettlements/RE_Kenshi.json`.
3. Ativar o mod no launcher do Kenshi. O RE_Kenshi lê o `RE_Kenshi.json`
   e injeta a DLL.
4. Diagnóstico: o plugin escreve `living_settlements.log` (POC-010) e
   espelha no debug log do RE_Kenshi.

## Regras de arquitetura (inegociáveis — ver ADRs no doc)

- **ADR-014**: todo acesso a estado do jogo acontece no hook da main thread
  (`GameWorld::mainLoop_GPUSensitiveStuff`). Nunca em worker thread.
- **ADR-013**: observação por polling/snapshot; não existe event-bus.
- **ADR-017**: autoridade do jogador é invariante duplo
  (`canTakePlayerOrdersAtThisTime` + `ActivePlatoon::isPlayer`); nunca
  sobrescrever `TP_OBEDIENCE`.
- **ADR-011**: versão de jogo desconhecida ⇒ **fail-closed** (nenhum hook
  arriscado é instalado; o plugin vira no-op com log).
- **REQ-PER-001**: nenhum ponteiro do processo é persistido — apenas ids
  estáveis (`hand::toString`/`InstanceID`).
- Doc §0.2: **nunca inventar métodos/offsets/capacidades** do FCS/KenshiLib.

## Estrutura (doc §19)

```
plugin/src/core/        tick host, snapshot, diagnóstico, version gate
plugin/src/domain/      núcleo puro testável (SEM dependência de KenshiLib)
plugin/src/adapters/    tradução intenção→TaskType (Etapa 0: stubs)
plugin/src/pocs/        provas técnicas POC-001..016
plugin/package/         arquivos de distribuição do mod
tests/unit/             pirâmide §16.1 nível 1
```

## Gate de arquitetura (doc §13.1)

A Etapa 0 termina com um veredito:
- **Gate A** — atribuição e observação de Jobs seguras → coordenador completo.
- **Gate B** — parcial → automação limitada + modo assistido.
- **Gate C** — reprovado → pacote FCS melhorado + diagnóstico.
