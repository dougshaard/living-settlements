Living Settlements - pacote do mod (Etapa 0)
=============================================

Conteudo desta pasta (copiar para <Kenshi>/mods/LivingSettlements/):

  LivingSettlements.mod   - mod FCS vazio (46 bytes; identico ao formato
                            dos exemplos oficiais do KenshiLib; o nome do
                            mod nao e embutido no arquivo). Alternativa
                            "oficial": criar um mod vazio com esse nome
                            no proprio FCS.
  RE_Kenshi.json          - manifesto lido pelo RE_Kenshi; carrega a DLL
                            via chave "Plugins" quando o mod esta ATIVO
                            no launcher. (O loader resolve o export
                            MSVC-mangled ?startPlugin@@YAXXZ.)
  LivingSettlements.dll   - (gerada pelo build; nao versionada no git)

Passos:
  1. Compilar plugin/LivingSettlements.sln em Release|x64 (toolset v100).
  2. Copiar a DLL de plugin/x64/Release/ para esta pasta.
  3. Copiar esta pasta para <Kenshi>/mods/LivingSettlements/.
  4. Ativar "LivingSettlements" no launcher do Kenshi.
  5. Requer RE_Kenshi 0.3.4+ instalado.

Diagnostico (POC-010):
  <Kenshi>/living_settlements.log  - log proprio do plugin
  Debug log do RE_Kenshi           - marcos e erros espelhados

Avisos:
  - Windows 11 com Smart App Control ativo pode bloquear o load da DLL.
  - Versao/plataforma desconhecida => o plugin NAO instala hooks
    (fail-closed, ADR-011) e registra o motivo no log.
