# Living Settlements

Um mod de automação de colônia para **Kenshi**. A ideia é simples: em vez de você
ficar microgerenciando quem opera cada máquina, o mod observa o assentamento e ajuda
o trabalho a fluir sozinho. Ele trabalha *por cima* da IA nativa do jogo, coordenando
os personagens entre si — reserva de postos pra duas pessoas não caírem na mesma vaga,
logística puxada, política de estoque e distribuição de tarefas na colônia inteira.

Roda como plugin nativo, carregado pelo [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi)
com os bindings do [KenshiLib](https://github.com/KenshiReclaimer/KenshiLib).

> **Em desenvolvimento.** Por enquanto o mod só *lê* o jogo e raciocina em segundo
> plano — calcula o que faria, mas ainda não dá nenhuma ordem. A escrita fica desligada
> até a segurança contra corrupção de save estar comprovada. Veja o estado abaixo.

## Estado

| Parte | Como está |
| --- | --- |
| Núcleo de reservas (código puro, testável) | Pronto — coberto por testes de unidade que rodam em qualquer compilador, sem o jogo |
| Plugin (carga, checagem de versão, tick na main thread, log) | Compila e roda no jogo (Release / x64) |
| Leitura da colônia (personagens, cargos, postos, produção, ameaças) | Validada em jogo (Kenshi 1.0.65 via RE_Kenshi) |
| Coordenação de trabalho (quadro de tarefas, pool, atribuição) | Rodando em modo sombra — calcula as decisões e registra no log, mas não emite |
| Emissão de ordens (escrita no jogo) | Implementada e desligada por padrão, atrás de um gate de segurança |

## Requisitos

- **Visual Studio 2019 ou mais novo**, com o toolset **Visual C++ 2010 (v100)**
  instalado. O binário do Kenshi usa a ABI do VS2010, então esse toolset é obrigatório.
- Os dependências do KenshiLib clonadas **ao lado** desta pasta, em
  `../KenshiLib_Examples_deps` — é o repositório
  [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps),
  que traz os includes do KenshiLib, do Ogre, do MyGUI e o Boost 1.60 pré-compilado.
  Rode o `Setup.bat` dele uma vez (ele define as variáveis de ambiente que o projeto usa).
- **Kenshi 1.0.65 ou 1.0.68** (Steam ou GOG) com o
  [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) 0.3.4 ou mais novo.

## Compilar

Abra a solution [plugin/LivingSettlements.sln](plugin/LivingSettlements.sln) no Visual
Studio, selecione **Release | x64** (o Debug não funciona com o KenshiLib) e compile.
A DLL sai em `plugin/x64/Release/LivingSettlements.dll`.

Pra rodar só os testes do núcleo — em qualquer máquina, sem precisar do jogo:

```sh
cd tests/unit
g++ -std=c++03 -Wall -I../../plugin/src \
    ../../plugin/src/domain/ReservationManager.cpp test_reservation.cpp \
    -o test_reservation && ./test_reservation
```

(ou o equivalente com `cl` no prompt do MSVC).

## Instalar no jogo

1. No FCS, crie um mod vazio chamado **LivingSettlements**. Isso gera o arquivo `.mod`
   dentro de `mods/LivingSettlements/`, na pasta do Kenshi.
2. Copie pra essa mesma pasta a `LivingSettlements.dll` que você compilou e o
   [RE_Kenshi.json](plugin/package/LivingSettlements/RE_Kenshi.json).
3. Ative o mod no launcher. O RE_Kenshi lê o `RE_Kenshi.json` e injeta a DLL.
4. Com o mod rodando, o plugin escreve um `living_settlements.log` na pasta do Kenshi —
   é onde dá pra acompanhar o que ele está lendo e decidindo.

## Como funciona (as decisões que importam)

- Todo acesso ao estado do jogo acontece num **único ponto, na main thread** (o hook do
  loop principal). Nunca em worker thread.
- Não existe event-bus no jogo, então o mod observa por **polling** e comparação de
  fotos do mundo entre um tick e outro.
- A **autoridade do jogador é sagrada**: o mod nunca sobrescreve uma ordem sua, nunca
  mexe em quem você selecionou e nunca encosta em unidades sob comando direto.
- Se a versão do jogo não for reconhecida, o plugin **não instala nada** e vira um no-op
  — só registra o motivo no log.
- Nenhum ponteiro do jogo é guardado entre frames; a identidade é sempre por id estável.
- O código **não inventa API do jogo**: tudo que ele chama foi conferido nos headers reais.

Organização do código:

```
plugin/src/core/      tick, diagnóstico, checagem de versão, gate de ciclo de vida
plugin/src/domain/    o núcleo puro e testável (sem nenhuma dependência do KenshiLib)
plugin/src/adapters/  a ponte entre o jogo e o núcleo (leitura e escrita)
plugin/src/pocs/      provas de leitura e de escrita rodadas no jogo
tests/unit/           testes do núcleo
plugin/package/       arquivos de distribuição do mod
```

## Licença

GPLv3 — veja o arquivo [LICENSE](LICENSE). O KenshiLib é GPLv3, então o mod também é.
