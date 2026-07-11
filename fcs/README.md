# fcs/

A parte de dados (FCS) do mod. Por padrão ela é aditiva — não altera registros
existentes do jogo, só acrescenta.

## Por enquanto

Nesta fase o mod só precisa de um **mod FCS vazio** pra o RE_Kenshi ter onde carregar
a DLL:

1. Abra o Forgotten Construction Set (FCS).
2. Crie um mod novo chamado `LivingSettlements` e salve. Isso cria o
   `mods/LivingSettlements/LivingSettlements.mod` na pasta do jogo.
3. O `.mod` é um binário do FCS e não é versionado aqui — só o processo fica documentado.

## Mais pra frente

- `mod/` — os registros FCS do mod (pacotes de IA de guarda, presets de profissão,
  edifícios e armazenamentos extras).
- `fcs_extended_plugins/` — plugins de autoria feita pelo FCS_extended.
