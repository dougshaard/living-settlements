Living Settlements - pacote do mod
==================================

Conteudo desta pasta (copiar para <Kenshi>/mods/LivingSettlements/):

  LivingSettlements.mod   - mod FCS vazio (mesmo formato dos exemplos oficiais do
                            KenshiLib; o nome do mod nao fica embutido no arquivo).
                            Alternativa: criar um mod vazio com esse nome no proprio FCS.
  RE_Kenshi.json          - manifesto lido pelo RE_Kenshi; carrega a DLL quando o mod
                            esta ativo no launcher. (O loader resolve o export
                            MSVC-mangled ?startPlugin@@YAXXZ.)
  LivingSettlements.dll   - (gerada pelo build; nao versionada no git)

Passos:
  1. Compilar a solution em Release|x64 (toolset v100).
  2. Copiar a DLL de plugin/x64/Release/ para esta pasta.
  3. Copiar esta pasta para <Kenshi>/mods/LivingSettlements/.
  4. Ativar "LivingSettlements" no launcher do Kenshi.
  5. Requer RE_Kenshi 0.3.4+ instalado.

Diagnostico:
  <Kenshi>/living_settlements.log  - log proprio do plugin
  Debug log do RE_Kenshi           - marcos e erros espelhados

Avisos:
  - Windows 11 com Smart App Control ativo pode bloquear o load da DLL.
  - Versao/plataforma desconhecida => o plugin NAO instala hooks (vira no-op) e
    registra o motivo no log.
